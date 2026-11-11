idf_component_register(SRCS "log_buffer.c" "log_hook.c"
                       INCLUDE_DIRS "include"
                       REQUIRES log
                       PRIV_REQUIRES freertos esp_hw_support)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
