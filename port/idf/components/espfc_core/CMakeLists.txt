set(ESPFC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../)

file(GLOB_RECURSE ESPFC_SRCS ${ESPFC_ROOT}/lib/Espfc/src/*.cpp)

idf_component_register(
  SRCS ${ESPFC_SRCS}
  INCLUDE_DIRS
    ${ESPFC_ROOT}/lib/Espfc/src
    ${ESPFC_ROOT}/lib/Hal/include
    ${ESPFC_ROOT}/lib/AHRS/src
    ${ESPFC_ROOT}/lib/Gps/src
    ${ESPFC_ROOT}/lib/betaflight/src
    ${ESPFC_ROOT}/lib/MultiButton/src
    ${ESPFC_ROOT}/lib/printf/src
  REQUIRES esp_driver_gpio esp_driver_uart esp_driver_i2c esp_driver_spi esp_driver_gptimer esp_driver_rmt
)

# target select: ESP32P4 branch in lib/Espfc/src/Target/Target.h
target_compile_definitions(${COMPONENT_LIB} PUBLIC ESP32P4)
# esp-fc keys most shared code on ESP32; P4 uses those paths except
# the RMT/timer drivers, which are replaced in this port.
target_compile_definitions(${COMPONENT_LIB} PUBLIC ESP32)
target_compile_definitions(${COMPONENT_LIB} PUBLIC F_CPU=360000000L)

# uint32_t is 'unsigned long' on riscv32; esp-fc's printf formats assume 'unsigned int'
target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-error=format)
