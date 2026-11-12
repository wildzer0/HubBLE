idf_component_register(SRCS "ble_central.c" "device_registry.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hogp_client hid_parser lat_stats pairing
                       PRIV_REQUIRES settings bt nvs_flash esp_timer hid_normalizer hid_report freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
