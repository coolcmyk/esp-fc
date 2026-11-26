set(ESPFC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../)

file(GLOB_RECURSE ESPFC_SRCS ${ESPFC_ROOT}/lib/Espfc/src/*.cpp)
# Platform Hal implementations for the P4 live in the same archive: the Hal
# symbols are referenced from the core objects, and a separate component made
# the static link order drop them.
file(GLOB_RECURSE HAL_SRCS ${CMAKE_CURRENT_LIST_DIR}/hal/*.cpp)
# generic Hal sources (bus helpers, EscDriver wrapper, board helpers)
list(APPEND HAL_SRCS
  ${ESPFC_ROOT}/lib/Hal/src/BusDevice.cpp
  ${ESPFC_ROOT}/lib/Hal/src/EscDriver.cpp
  ${ESPFC_ROOT}/lib/Hal/src/Board.cpp
)
file(GLOB_RECURSE AHRS_GPS_C ${ESPFC_ROOT}/lib/AHRS/src/*.c ${ESPFC_ROOT}/lib/Gps/src/*.c)
file(GLOB_RECURSE AHRS_GPS_CPP ${ESPFC_ROOT}/lib/AHRS/src/*.cpp ${ESPFC_ROOT}/lib/Gps/src/*.cpp)

# Betaflight's platform glue defines the parameter groups and the C shims that
# esp-fc relies on. blackbox is kept; io/serial_4way* is not (esp-fc replaces it).
file(GLOB BF_GLUE ${ESPFC_ROOT}/lib/betaflight/src/*.c
                   ${ESPFC_ROOT}/lib/betaflight/src/blackbox/*.c
                   ${ESPFC_ROOT}/lib/betaflight/src/io/*.c)
list(APPEND BF_GLUE ${ESPFC_ROOT}/lib/betaflight/src/platform_cpp.cpp)
# esp-fc's minimal printf (tfp_format) used by the CLI/MSP formatting
list(APPEND BF_GLUE ${ESPFC_ROOT}/lib/printf/src/printf.c)

idf_component_register(
  SRCS ${ESPFC_SRCS} ${HAL_SRCS} ${PRINTF_SRC} ${AHRS_GPS_C} ${AHRS_GPS_CPP} ${BF_GLUE}
  INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/hal
    ${ESPFC_ROOT}/lib/Espfc/src
    ${ESPFC_ROOT}/lib/Hal/include
    ${ESPFC_ROOT}/lib/AHRS/src
    ${ESPFC_ROOT}/lib/Gps/src
    ${ESPFC_ROOT}/lib/betaflight/src
    ${ESPFC_ROOT}/lib/MultiButton/src
    ${ESPFC_ROOT}/lib/printf/src
  REQUIRES esp_driver_gpio esp_driver_uart esp_driver_i2c esp_driver_gptimer
           esp_driver_rmt nvs_flash esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC ESP32P4)
target_compile_definitions(${COMPONENT_LIB} PUBLIC ESP32)
target_compile_definitions(${COMPONENT_LIB} PUBLIC F_CPU=360000000L)
target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-error=format)
