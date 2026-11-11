idf_component_register(SRCS "board_marker.c" "boot_guard.c" "ota.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_update esp_system
                       PRIV_REQUIRES board settings wifi_ap semver esp_partition esp_app_format bootloader_support freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
