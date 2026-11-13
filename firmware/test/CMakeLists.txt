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
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(unity cjson)

# Sanitizers for every host test: -DHUBBLE_SANITIZE=ON (AddressSanitizer incl. leak check, and UBSan).
option(HUBBLE_SANITIZE "Build all host tests with ASan/LSan/UBSan" OFF)
if(HUBBLE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

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

hubble_add_test(test_wifi_scan_list unit/test_wifi_scan_list.c ${COMP}/wifi_ap/wifi_scan_list.c)
target_include_directories(test_wifi_scan_list PRIVATE ${COMP}/wifi_ap/include)

hubble_add_test(test_dns_reply unit/test_dns_reply.c ${COMP}/captive_portal/dns_reply.c)
target_include_directories(test_dns_reply PRIVATE ${COMP}/captive_portal/include)

hubble_add_test(test_input_filter unit/test_input_filter.c ${COMP}/input_filter/input_filter.c)
target_include_directories(test_input_filter PRIVATE ${COMP}/input_filter/include)

hubble_add_test(test_captive_probe unit/test_captive_probe.c ${COMP}/captive_portal/captive_probe.c)
target_include_directories(test_captive_probe PRIVATE ${COMP}/captive_portal/include)

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

# --- M4 pure-logic components ---------------------------------------------------
hubble_add_test(test_pairing unit/test_pairing.c ${COMP}/pairing/pairing.c)
target_include_directories(test_pairing PRIVATE ${COMP}/pairing/include)

# --- M6 pure-logic components ---------------------------------------------------
hubble_add_test(test_ota_manifest unit/test_ota_manifest.c ${COMP}/ota_manifest/ota_manifest.c ${COMP}/semver/semver.c)
target_include_directories(test_ota_manifest PRIVATE ${COMP}/ota_manifest/include ${COMP}/semver/include)
target_include_directories(test_ota_manifest PRIVATE ${cjson_SOURCE_DIR})
target_link_libraries(test_ota_manifest PRIVATE cjson)

# --- M7 pure-logic components ---------------------------------------------------
hubble_add_test(test_power_policy unit/test_power_policy.c ${COMP}/power/power_policy.c)
target_include_directories(test_power_policy PRIVATE ${COMP}/power/include)

hubble_add_test(test_led_pattern unit/test_led_pattern.c ${COMP}/led/led_pattern.c)
target_include_directories(test_led_pattern PRIVATE ${COMP}/led/include)

# --- Fuzz targets (CLAUDE.md §5.3) -----------------------------------------------
# -DHUBBLE_FUZZ=ON builds one executable per target. With Clang they are real libFuzzer binaries (coverage guided);
# with other compilers they link a small mutation driver (tools/fuzz.sh runs either kind).
option(HUBBLE_FUZZ "Build the fuzz targets" OFF)
if(HUBBLE_FUZZ)
    function(hubble_add_fuzzer name src)
        add_executable(${name} ${src} ${ARGN})
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -g)
            target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
        else()
            target_sources(${name} PRIVATE fuzz/fuzz_driver.c)
            target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -g)
            target_link_options(${name} PRIVATE -fsanitize=address,undefined)
        endif()
    endfunction()

    hubble_add_fuzzer(fuzz_hid_parser fuzz/fuzz_hid_parser.c ${COMP}/hid_parser/hid_parser.c
                      ${COMP}/hid_normalizer/hid_normalizer.c ${COMP}/hid_report/hid_report.c)
    target_include_directories(fuzz_hid_parser PRIVATE ${COMP}/hid_parser/include ${COMP}/hid_normalizer/include
                               ${COMP}/hid_report/include)

    hubble_add_fuzzer(fuzz_dns_reply fuzz/fuzz_dns_reply.c ${COMP}/captive_portal/dns_reply.c)
    target_include_directories(fuzz_dns_reply PRIVATE ${COMP}/captive_portal/include)

    hubble_add_fuzzer(fuzz_ota_manifest fuzz/fuzz_ota_manifest.c ${COMP}/ota_manifest/ota_manifest.c
                      ${COMP}/semver/semver.c ${cjson_SOURCE_DIR}/cJSON.c)
    target_include_directories(fuzz_ota_manifest PRIVATE ${COMP}/ota_manifest/include ${COMP}/semver/include
                               ${cjson_SOURCE_DIR})
endif()
