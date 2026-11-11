# Embeds the packed, gzipped web app (tools/pack_webapp.py) into the firmware.
set(WEBAPP_BUNDLE "${CMAKE_CURRENT_LIST_DIR}/generated/webapp.bundle.bin")
if(NOT EXISTS "${WEBAPP_BUNDLE}")
    message(FATAL_ERROR "Web app bundle not found: ${WEBAPP_BUNDLE}\n"
                        "Build the web app and run tools/pack_webapp.py first "
                        "(tools/build-firmware.sh does both).")
endif()

idf_component_register(SRCS "web_bundle.c" "web_assets.c"
                       INCLUDE_DIRS "include"
                       EMBED_FILES "${WEBAPP_BUNDLE}")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
