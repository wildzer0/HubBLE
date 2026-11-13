idf_component_register(SRCS "usb_hid.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hid_report
                       PRIV_REQUIRES settings esp_tinyusb freertos esp_timer log)
