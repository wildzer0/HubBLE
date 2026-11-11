idf_component_register(SRCS "led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_driver_gpio
                       PRIV_REQUIRES led_strip)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
