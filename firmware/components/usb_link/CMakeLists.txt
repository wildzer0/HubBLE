idf_component_register(SRCS "ulink_proto.c" "usb_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES usb_hid wifi_ap web_server json lwip esp_app_format board freertos log esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
