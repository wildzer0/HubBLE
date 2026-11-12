idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES usb_hid board led web_assets esp_app_format app_update esp_system
                                settings log_buffer ota wifi_ap captive_portal web_server)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
