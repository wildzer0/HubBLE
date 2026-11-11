idf_component_register(SRCS "settings.c" "settings_model.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
