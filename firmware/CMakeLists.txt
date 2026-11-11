# HubBLE firmware — top-level ESP-IDF project.
#
# Board selection: pass -DHUBBLE_BOARD=<s3_zero|xiao_s3> (default: s3_zero).
# The helper script tools/build-firmware.sh wraps this and uses a separate
# build directory per board (build-<board>/).
cmake_minimum_required(VERSION 3.16)

# --- Board selection ---------------------------------------------------------
if(NOT DEFINED HUBBLE_BOARD)
    set(HUBBLE_BOARD "s3_zero")
endif()
if(NOT HUBBLE_BOARD MATCHES "^(s3_zero|xiao_s3)$")
    message(FATAL_ERROR "Unknown HUBBLE_BOARD '${HUBBLE_BOARD}' (expected s3_zero or xiao_s3)")
endif()

# Common defaults first, then board-specific ones (later files override).
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.${HUBBLE_BOARD}")
# Each board keeps its own generated sdkconfig inside its build directory so
# switching boards never reuses a stale configuration.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

# --- Version -----------------------------------------------------------------
# Release builds pass -DHUBBLE_VERSION=X.Y.Z (derived from the git tag by CI).
# Local builds derive it from `git describe`, falling back to 0.0.0-unknown.
if(NOT DEFINED HUBBLE_VERSION)
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE HUBBLE_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT HUBBLE_VERSION)
        set(HUBBLE_VERSION "0.0.0-unknown")
    endif()
endif()
string(REGEX REPLACE "^v" "" HUBBLE_VERSION "${HUBBLE_VERSION}")
set(PROJECT_VER "${HUBBLE_VERSION}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hubble)
