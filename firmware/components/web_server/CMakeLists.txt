idf_component_register(SRCS "web_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server
                       PRIV_REQUIRES api web_assets wifi_ap log_buffer json diag board esp_timer freertos lwip)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
