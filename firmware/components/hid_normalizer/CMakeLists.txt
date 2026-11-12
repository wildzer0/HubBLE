idf_component_register(SRCS "hid_normalizer.c" INCLUDE_DIRS "include" REQUIRES hid_parser hid_report)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
