#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start recording motion-JPEG to /sdcard/<name>.mjpeg in a background task.
// Initialises the SD card and the camera on first use.
esp_err_t video_rec_start(const char *name);
esp_err_t video_rec_stop(void);
bool video_rec_active(void);

// Frames and bytes of the running (or last) clip.
void video_rec_stats(unsigned *frames, unsigned *bytes);

// Writes a newline separated listing of /sdcard/*.mjpeg into out.
esp_err_t video_rec_list(char *out, size_t len);

#ifdef __cplusplus
}
#endif
