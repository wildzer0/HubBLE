idf_component_register(SRCS "ap_policy.c" "wifi_scan_list.c" "wifi_ap.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event settings
                       PRIV_REQUIRES esp_timer esp_hw_support freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
