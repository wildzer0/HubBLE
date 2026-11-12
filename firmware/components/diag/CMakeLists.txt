idf_component_register(SRCS "diag.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_system
                       PRIV_REQUIRES esp_driver_tsens esp_timer heap soc app_update esp_partition)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
