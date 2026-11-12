idf_component_register(SRCS "ota_manifest.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES semver json)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
