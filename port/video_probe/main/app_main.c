/*
 * Orion video recorder test for the Waveshare ESP32-P4-NANO:
 * OV5647 (MIPI-CSI) -> hardware JPEG encoder -> motion-JPEG file on the SD card.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/jpeg_encode.h"
#include "driver/sdmmc_host.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_vfs_fat.h"
#include "linux/videodev2.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG = "orion_cam";

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
#define RECORD_FRAMES 45
#define JPEG_MAX (512 * 1024)

static esp_ldo_channel_handle_t s_ldo;
static sdmmc_card_t *s_card;

static esp_err_t camera_start(int *fd_out) {
  esp_ldo_channel_config_t ldo_cfg = {.chan_id = CAM_LDO_CHAN, .voltage_mv = CAM_LDO_MV};
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_ldo), TAG, "ldo");

  esp_cam_sensor_xclk_handle_t xclk;
  ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &xclk), TAG, "xclk alloc");
  esp_cam_sensor_xclk_config_t xcfg = {.esp_clock_router_cfg = {.xclk_pin = CAM_XCLK_PIN, .xclk_freq_hz = CAM_XCLK_FREQ}};
  ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_start(xclk, &xcfg), TAG, "xclk start");
  vTaskDelay(pdMS_TO_TICKS(20));

  static esp_video_init_csi_config_t csi = {
      .sccb_config = {.init_sccb = true,
                      .i2c_config = {.port = CAM_I2C_PORT, .scl_pin = CAM_I2C_SCL, .sda_pin = CAM_I2C_SDA},
                      .freq = 100000},
      .reset_pin = -1,
      .pwdn_pin = -1,
      .dont_init_ldo = true,
  };
  static const esp_video_init_config_t cfg = {.csi = &csi};
  ESP_RETURN_ON_ERROR(esp_video_init(&cfg), TAG, "esp_video_init");

  const int fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
  if (fd < 0) {
    ESP_LOGE(TAG, "no sensor on %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
    return ESP_ERR_NOT_FOUND;
  }

  struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                            .fmt.pix = {.width = WIDTH, .height = HEIGHT,
                                        .pixelformat = V4L2_PIX_FMT_RGB565,
                                        .field = V4L2_FIELD_NONE}};
  ESP_RETURN_ON_FALSE(ioctl(fd, VIDIOC_S_FMT, &fmt) == 0, ESP_FAIL, TAG, "S_FMT");
  ESP_LOGI(TAG, "capture: %lux%lu RGB565", (unsigned long)fmt.fmt.pix.width, (unsigned long)fmt.fmt.pix.height);
  *fd_out = fd;
  return ESP_OK;
}

static esp_err_t sd_mount(void) {
  gpio_config_t pwr = {.pin_bit_mask = 1ULL << SD_POWER_PIN, .mode = GPIO_MODE_OUTPUT};
  gpio_config(&pwr);
  gpio_set_level(SD_POWER_PIN, 0);  // active low power switch
  vTaskDelay(pdMS_TO_TICKS(200));

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;

  // The slot's IO rail comes from on-chip LDO channel 4 (LDO_VO4) on this
  // board; the GPIO is only the load switch. Without the LDO the card never
  // answers and init times out with 0x107.
  static sd_pwr_ctrl_handle_t sd_pwr = NULL;
  sd_pwr_ctrl_ldo_config_t pwr_cfg = {.ldo_chan_id = 4};
  esp_err_t perr = sd_pwr_ctrl_new_on_chip_ldo(&pwr_cfg, &sd_pwr);
  if (perr != ESP_OK) {
    ESP_LOGW(TAG, "sd ldo power ctrl failed: %s", esp_err_to_name(perr));
  } else {
    host.pwr_ctrl_handle = sd_pwr;
  }
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t mnt = {
      .format_if_mount_failed = false,
      .max_files = 4,
      .allocation_unit_size = 16 * 1024,
  };
  return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mnt, &s_card);
}

void app_main(void) {
  ESP_LOGI(TAG, "=== orion recorder: OV5647 -> JPEG -> SD ===");

  if (sd_mount() != ESP_OK) {
    ESP_LOGE(TAG, "SD mount failed");
    return;
  }
  ESP_LOGI(TAG, "SD mounted: %llu MB", ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024));

  int cam_fd = -1;
  if (camera_start(&cam_fd) != ESP_OK) return;

  struct v4l2_requestbuffers req = {.count = CAPTURE_BUFFERS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
  if (ioctl(cam_fd, VIDIOC_REQBUFS, &req) != 0) {
    ESP_LOGE(TAG, "REQBUFS failed (PSRAM?)");
    return;
  }
  void *bufs[CAPTURE_BUFFERS] = {0};
  size_t lens[CAPTURE_BUFFERS] = {0};
  for (int i = 0; i < CAPTURE_BUFFERS; ++i) {
    struct v4l2_buffer b = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = (uint32_t)i};
    if (ioctl(cam_fd, VIDIOC_QUERYBUF, &b) != 0) return;
    bufs[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam_fd, b.m.offset);
    lens[i] = b.length;
    ioctl(cam_fd, VIDIOC_QBUF, &b);
  }
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(cam_fd, VIDIOC_STREAMON, &type) != 0) {
    ESP_LOGE(TAG, "STREAMON failed");
    return;
  }

  jpeg_encoder_handle_t jenc = NULL;
  jpeg_encode_engine_cfg_t ecfg = {.timeout_ms = 200};
  if (jpeg_new_encoder_engine(&ecfg, &jenc) != ESP_OK) {
    ESP_LOGE(TAG, "jpeg encoder init failed");
    return;
  }
  jpeg_encode_cfg_t jcfg = {.height = HEIGHT,
                            .width = WIDTH,
                            .src_type = JPEG_ENC_SRC_RGB565,
                            .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
                            .image_quality = 70};
  // The hardware encoder requires an aligned output buffer.
  jpeg_encode_memory_alloc_cfg_t mem_cfg = {
      .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
  };
  size_t jpeg_size = 0;
  uint8_t *jpeg = (uint8_t *)jpeg_alloc_encoder_mem(JPEG_MAX, &mem_cfg, &jpeg_size);
  if (!jpeg) {
    ESP_LOGE(TAG, "no PSRAM for jpeg buffer");
    return;
  }

  FILE *f = fopen("/sdcard/orion_clip.mjpeg", "wb");
  if (!f) {
    ESP_LOGE(TAG, "cannot open file on SD");
    return;
  }

  int written = 0;
  size_t total = 0;
  const TickType_t start = xTaskGetTickCount();
  for (int n = 0; n < RECORD_FRAMES; ++n) {
    struct v4l2_buffer b = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
    if (ioctl(cam_fd, VIDIOC_DQBUF, &b) != 0) break;
    uint32_t jsize = 0;
    if (jpeg_encoder_process(jenc, &jcfg, (const uint8_t *)bufs[b.index], (uint32_t)lens[b.index], jpeg, (uint32_t)jpeg_size, &jsize) == ESP_OK) {
      fwrite(jpeg, 1, jsize, f);
      total += jsize;
      ++written;
    }
    ioctl(cam_fd, VIDIOC_QBUF, &b);
  }
  fclose(f);

  // Read the file back and count JPEG start markers: proves the clip is a real
  // motion-JPEG rather than a byte dump.
  {
    FILE *r = fopen("/sdcard/orion_clip.mjpeg", "rb");
    if (r) {
      int soi = 0, prev = -1, c;
      long bytes = 0;
      while ((c = fgetc(r)) != EOF) {
        ++bytes;
        if (prev == 0xFF && c == 0xD8) ++soi;
        prev = c;
      }
      fclose(r);
      ESP_LOGI(TAG, "verify: %ld bytes, %d JPEG SOI markers", bytes, soi);
    } else {
      ESP_LOGE(TAG, "verify: cannot reopen clip");
    }
  }

  const uint32_t ms = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
  ESP_LOGI(TAG, "recorded %d frames, %u bytes, %lu ms (%.1f fps)", written, (unsigned)total,
           (unsigned long)ms, ms ? written * 1000.0f / ms : 0.0f);

  ioctl(cam_fd, VIDIOC_STREAMOFF, &type);
  jpeg_del_encoder_engine(jenc);
  free(jpeg);
  close(cam_fd);
  esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
  ESP_LOGI(TAG, "done");
}
