idf_component_register(SRCS "ota_online.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ota_manifest
                       PRIV_REQUIRES ota settings wifi_ap ble_central board esp_http_client esp_https_ota mbedtls
                                     esp-tls esp_app_format app_update freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
