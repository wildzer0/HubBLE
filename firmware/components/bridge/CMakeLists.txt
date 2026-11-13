idf_component_register(SRCS "bridge.c" INCLUDE_DIRS "include"
                       REQUIRES esp_common power
                       PRIV_REQUIRES board esp_driver_gpio ble_central ota_online usb_hid hid_normalizer hid_report combo wifi_ap settings esp_timer freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
