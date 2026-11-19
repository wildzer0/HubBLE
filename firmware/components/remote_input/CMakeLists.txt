idf_component_register(SRCS "ascii_hid.c" "remote_input.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json
                       PRIV_REQUIRES usb_hid hid_report bridge esp_timer freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
