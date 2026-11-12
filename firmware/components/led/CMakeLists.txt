idf_component_register(SRCS "led.c" "led_pattern.c" "led_status.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_driver_gpio
                       PRIV_REQUIRES led_strip esp_timer freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
