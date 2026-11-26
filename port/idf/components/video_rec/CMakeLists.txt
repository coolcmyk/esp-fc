idf_component_register(
  SRCS "video_rec.c"
  INCLUDE_DIRS "."
  REQUIRES esp_driver_jpeg esp_driver_sdmmc sdmmc fatfs esp_driver_gpio
)
