# Host unit tests (plain CMake + Unity) for the pure-logic components.
# These components must not depend on ESP-IDF APIs (CLAUDE.md §3.1).
# Run:  cmake -S firmware/test -B firmware/test/build && cmake --build firmware/test/build && ctest --test-dir firmware/test/build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(hubble_host_tests C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.1)
FetchContent_MakeAvailable(unity)

enable_testing()

# hubble_add_test(<name> <test source> <component sources...>)
function(hubble_add_test name test_src)
    add_executable(${name} ${test_src} ${ARGN})
    target_link_libraries(${name} PRIVATE unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

set(COMP ${CMAKE_CURRENT_LIST_DIR}/../components)

hubble_add_test(test_semver unit/test_semver.c ${COMP}/semver/semver.c)
target_include_directories(test_semver PRIVATE ${COMP}/semver/include)

hubble_add_test(test_web_bundle unit/test_web_bundle.c ${COMP}/web_assets/web_bundle.c)
target_include_directories(test_web_bundle PRIVATE ${COMP}/web_assets/include)

# --- M1 pure-logic components ---------------------------------------------------
hubble_add_test(test_log_buffer unit/test_log_buffer.c ${COMP}/log_buffer/log_buffer.c)
target_include_directories(test_log_buffer PRIVATE ${COMP}/log_buffer/include)

hubble_add_test(test_ap_policy unit/test_ap_policy.c ${COMP}/wifi_ap/ap_policy.c)
target_include_directories(test_ap_policy PRIVATE ${COMP}/wifi_ap/include)

hubble_add_test(test_dns_reply unit/test_dns_reply.c ${COMP}/captive_portal/dns_reply.c)
target_include_directories(test_dns_reply PRIVATE ${COMP}/captive_portal/include)

hubble_add_test(test_settings_model unit/test_settings_model.c ${COMP}/settings/settings_model.c)
target_include_directories(test_settings_model PRIVATE ${COMP}/settings/include)

hubble_add_test(test_http_guard unit/test_http_guard.c ${COMP}/api/http_guard.c)
target_include_directories(test_http_guard PRIVATE ${COMP}/api/include)

hubble_add_test(test_ota_logic unit/test_ota_logic.c ${COMP}/ota/board_marker.c ${COMP}/ota/boot_guard.c)
target_include_directories(test_ota_logic PRIVATE ${COMP}/ota/include)

# --- M2 pure-logic components ---------------------------------------------------
hubble_add_test(test_hid_report unit/test_hid_report.c ${COMP}/hid_report/hid_report.c)
target_include_directories(test_hid_report PRIVATE ${COMP}/hid_report/include)

# --- M3 pure-logic components ---------------------------------------------------
# The parser handles untrusted input: build its test with AddressSanitizer + UBSan.
hubble_add_test(test_hid_parser unit/test_hid_parser.c ${COMP}/hid_parser/hid_parser.c
                ${COMP}/hid_normalizer/hid_normalizer.c ${COMP}/hid_report/hid_report.c)
target_include_directories(test_hid_parser PRIVATE ${COMP}/hid_parser/include ${COMP}/hid_normalizer/include
                           ${COMP}/hid_report/include)
target_compile_options(test_hid_parser PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -g)
target_link_options(test_hid_parser PRIVATE -fsanitize=address,undefined)

hubble_add_test(test_combo_lat unit/test_combo_lat.c ${COMP}/combo/combo.c ${COMP}/lat_stats/lat_stats.c)
target_include_directories(test_combo_lat PRIVATE ${COMP}/combo/include ${COMP}/lat_stats/include)
