set(ESPFC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../)

# Platform implementations of the Hal interfaces for the ESP32-P4, replacing
# lib/Hal/src/ESP32/* which is written against the Arduino-ESP32 core.
idf_component_register(
  SRCS "hal_serial_p4.cpp" "hal_gpio_p4.cpp" "hal_hwtimer_p4.cpp"
  INCLUDE_DIRS ${ESPFC_ROOT}/lib/Hal/include ${ESPFC_ROOT}/lib/Espfc/src
  REQUIRES esp_driver_uart esp_driver_gpio esp_driver_gptimer espfc_core
)
