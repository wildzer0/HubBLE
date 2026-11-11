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
