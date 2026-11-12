idf_component_register(SRCS "hogp_client.c" INCLUDE_DIRS "include"
                       REQUIRES hid_parser bt
                       PRIV_REQUIRES hid_normalizer hid_report log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
