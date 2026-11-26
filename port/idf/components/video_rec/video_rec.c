// Motion-JPEG recorder for orion-fc: OV5647 (MIPI-CSI) -> hardware JPEG -> SD.
#include "video_rec.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/jpeg_encode.h"
#include "driver/sdmmc_host.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

// Provided by the FC's Hal: the I2C bus it already owns (GY-91 lives there).
extern i2c_master_bus_handle_t orion_i2c_bus(void);

static const char *TAG = "video";

#define CAM_I2C_PORT 0
#define CAM_I2C_SDA 7
#define CAM_I2C_SCL 8
#define CAM_XCLK_PIN 15
#define CAM_XCLK_FREQ 24000000
#define CAM_LDO_CHAN 3
#define CAM_LDO_MV 2500
#define SD_POWER_PIN 45

#define WIDTH 800
#define HEIGHT 800
#define CAPTURE_BUFFERS 2
#define JPEG_MAX (512 * 1024)

static esp_ldo_channel_handle_t s_cam_ldo;
static sdmmc_card_t *s_card;
static bool s_sd_mounted;
static bool s_cam_ready;
static int s_cam_fd = -1;
static jpeg_encoder_handle_t s_jenc;
static uint8_t *s_jpeg;
static size_t s_jpeg_size;
static void *s_bufs[CAPTURE_BUFFERS];
static size_t s_lens[CAPTURE_BUFFERS];

static volatile bool s_active;
static TaskHandle_t s_task;
static unsigned s_frames;
static unsigned s_bytes;

static esp_err_t sd_mount(void) {
  if (s_sd_mounted) return ESP_OK;

  gpio_config_t pwr = {.pin_bit_mask = 1ULL << SD_POWER_PIN, .mode = GPIO_MODE_OUTPUT};
  gpio_config(&pwr);
  gpio_set_level(SD_POWER_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(50));

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.flags = SDMMC_HOST_FLAG_4BIT;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  // IO rail comes from LDO_VO4 on this board; the GPIO only drives the switch.
  static sd_pwr_ctrl_handle_t sd_pwr = NULL;
  sd_pwr_ctrl_ldo_config_t pwr_cfg = {.ldo_chan_id = 4};
  if (sd_pwr_ctrl_new_on_chip_ldo(&pwr_cfg, &sd_pwr) == ESP_OK) {
    host.pwr_ctrl_handle = sd_pwr;
  }

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t mnt = {
      .format_if_mount_failed = false,
      .max_files = 4,
      .allocation_unit_size = 16 * 1024,
  };
  ESP_RETURN_ON_ERROR(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mnt, &s_card), TAG, "sd mount");
  s_sd_mounted = true;
  ESP_LOGI(TAG, "SD mounted");
  return ESP_OK;
}

static esp_err_t cam_init(void) {
  if (s_cam_ready) return ESP_OK;

  esp_ldo_channel_config_t ldo_cfg = {.chan_id = CAM_LDO_CHAN, .voltage_mv = CAM_LDO_MV};
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_cam_ldo), TAG, "cam ldo");

  // No P4-driven XCLK: GPIO15 belongs to the C6 SDIO link. Test whether the
  // module free-runs with the CSI configuration alone.
  ESP_LOGW(TAG, "XCLK not driven (GPIO15 kept for C6 SDIO)");

  static esp_video_init_csi_config_t csi = {
      .sccb_config = {.init_sccb = true,
                      .i2c_config = {.port = CAM_I2C_PORT, .scl_pin = CAM_I2C_SCL, .sda_pin = CAM_I2C_SDA},
                      .freq = 100000},
      .reset_pin = -1,
      .pwdn_pin = -1,
      .dont_init_ldo = true,
  };
  i2c_master_bus_handle_t shared = orion_i2c_bus();
  if (shared) {
    csi.sccb_config.init_sccb = false;
    csi.sccb_config.i2c_handle = shared;
  }
  static const esp_video_init_config_t cfg = {.csi = &csi};
  ESP_RETURN_ON_ERROR(esp_video_init(&cfg), TAG, "esp_video_init");

  s_cam_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
  if (s_cam_fd < 0) {
    ESP_LOGE(TAG, "no camera on %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
    return ESP_ERR_NOT_FOUND;
  }

  struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                            .fmt.pix = {.width = WIDTH, .height = HEIGHT,
                                        .pixelformat = V4L2_PIX_FMT_RGB565,
                                        .field = V4L2_FIELD_NONE}};
  ESP_RETURN_ON_FALSE(ioctl(s_cam_fd, VIDIOC_S_FMT, &fmt) == 0, ESP_FAIL, TAG, "S_FMT");

  struct v4l2_requestbuffers req = {.count = CAPTURE_BUFFERS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
  ESP_RETURN_ON_FALSE(ioctl(s_cam_fd, VIDIOC_REQBUFS, &req) == 0, ESP_FAIL, TAG, "REQBUFS");
  for (int i = 0; i < CAPTURE_BUFFERS; ++i) {
    struct v4l2_buffer b = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = (uint32_t)i};
    ESP_RETURN_ON_FALSE(ioctl(s_cam_fd, VIDIOC_QUERYBUF, &b) == 0, ESP_FAIL, TAG, "QUERYBUF");
    s_bufs[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_cam_fd, b.m.offset);
    s_lens[i] = b.length;
    ioctl(s_cam_fd, VIDIOC_QBUF, &b);
  }

  jpeg_encode_engine_cfg_t ecfg = {.timeout_ms = 200};
  ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&ecfg, &s_jenc), TAG, "jpeg engine");
  jpeg_encode_memory_alloc_cfg_t mem_cfg = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
  s_jpeg = (uint8_t *)jpeg_alloc_encoder_mem(JPEG_MAX, &mem_cfg, &s_jpeg_size);
  ESP_RETURN_ON_FALSE(s_jpeg != NULL, ESP_ERR_NO_MEM, TAG, "jpeg buffer");

  s_cam_ready = true;
  ESP_LOGI(TAG, "camera ready (%dx%d RGB565)", WIDTH, HEIGHT);
  return ESP_OK;
}

static void record_task(void *arg) {
  char path[64];
  snprintf(path, sizeof(path), "/sdcard/%s", (const char *)arg);
  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "cannot open %s", path);
    s_active = false;
    vTaskDelete(NULL);
    return;
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ESP_LOGI(TAG, "recording to %s", path);
  while (s_active) {
    struct v4l2_buffer b = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
    if (ioctl(s_cam_fd, VIDIOC_DQBUF, &b) != 0) break;

    jpeg_encode_cfg_t jcfg = {.height = HEIGHT,
                              .width = WIDTH,
                              .src_type = JPEG_ENC_SRC_RGB565,
                              .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
                              .image_quality = 70};
    uint32_t jsize = 0;
    if (jpeg_encoder_process(s_jenc, &jcfg, (const uint8_t *)s_bufs[b.index], (uint32_t)s_lens[b.index], s_jpeg,
                             (uint32_t)s_jpeg_size, &jsize) == ESP_OK) {
      fwrite(s_jpeg, 1, jsize, f);
      s_bytes += jsize;
      ++s_frames;
    }
    ioctl(s_cam_fd, VIDIOC_QBUF, &b);
  }

  fclose(f);
  ESP_LOGI(TAG, "stopped: %u frames, %u bytes", s_frames, s_bytes);
  vTaskDelete(NULL);
}

esp_err_t video_rec_start(const char *name) {
  if (s_active) return ESP_ERR_INVALID_STATE;
  ESP_RETURN_ON_ERROR(sd_mount(), TAG, "sd");
  ESP_RETURN_ON_ERROR(cam_init(), TAG, "cam");

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ESP_RETURN_ON_FALSE(ioctl(s_cam_fd, VIDIOC_STREAMON, &type) == 0, ESP_FAIL, TAG, "STREAMON");

  s_frames = 0;
  s_bytes = 0;
  s_active = true;
  char *arg = strdup(name && *name ? name : "orion_clip.mjpeg");
  if (!arg) {
    s_active = false;
    return ESP_ERR_NO_MEM;
  }
  // Pin to core 1 so SD writes and JPEG encoding do not compete with the
  // control loop and the network stack on core 0.
  if (xTaskCreatePinnedToCore(record_task, "video_rec", 8192, arg, 3, &s_task, 1) != pdPASS) {
    free(arg);
    s_active = false;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t video_rec_stop(void) {
  if (!s_active) return ESP_OK;
  s_active = false;
  vTaskDelay(pdMS_TO_TICKS(300));
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(s_cam_fd, VIDIOC_STREAMOFF, &type);

  // STREAMOFF returns ownership of every buffer; re-queue them so the next
  // start() has frames to dequeue (without this a restart records 0 frames).
  for (int i = 0; i < CAPTURE_BUFFERS; ++i) {
    struct v4l2_buffer b = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                            .memory = V4L2_MEMORY_MMAP,
                            .index = (uint32_t)i};
    ioctl(s_cam_fd, VIDIOC_QBUF, &b);
  }
  return ESP_OK;
}

bool video_rec_active(void) { return s_active; }

void video_rec_stats(unsigned *frames, unsigned *bytes) {
  if (frames) *frames = s_frames;
  if (bytes) *bytes = s_bytes;
}

esp_err_t video_rec_list(char *out, size_t len) {
  ESP_RETURN_ON_ERROR(sd_mount(), TAG, "sd");
  out[0] = '\0';
  DIR *dir = opendir("/sdcard");
  if (!dir) return ESP_FAIL;
  struct dirent *e;
  size_t used = 0;
  while ((e = readdir(dir)) != NULL) {
    const char *dot = strrchr(e->d_name, '.');
    if (!dot || strcmp(dot, ".mjpeg") != 0) continue;
    char full[300];
    snprintf(full, sizeof(full), "/sdcard/%s", e->d_name);
    struct stat st;
    long size = stat(full, &st) == 0 ? (long)st.st_size : 0;
    int n = snprintf(out + used, len - used, "%s %ld\n", e->d_name, size);
    if (n <= 0 || used + n >= len) break;
    used += n;
  }
  closedir(dir);
  return ESP_OK;
}
