idf_component_register(SRCS "main.cpp" "esc_pwm_rmt.c" "loop_timer.c"
                       INCLUDE_DIRS "."
                       REQUIRES espfc_core esp_driver_rmt esp_driver_gptimer)
