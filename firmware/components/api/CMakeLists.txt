idf_component_register(SRCS "http_guard.c" "api.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server
                       PRIV_REQUIRES json board ota diag settings wifi_ap log_buffer esp_timer esp_system freertos usb_hid hid_report ble_central)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
