idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES board led semver web_assets esp_app_format app_update esp_system)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
