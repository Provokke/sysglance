# Optional CMake build. The Makefile is the primary build and also provides
# the sanitizer, valgrind, fuzz and bench targets.
cmake_minimum_required(VERSION 3.16)
project(sysglance VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Werror -Wshadow -Wpointer-arith
                    -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef)

add_library(sg STATIC src/util.c src/parse.c src/config.c src/modules.c src/logo.c)
target_include_directories(sg PUBLIC src)
target_link_libraries(sg PUBLIC m)

add_executable(sysglance src/main.c)
target_link_libraries(sysglance PRIVATE sg)

add_executable(test_unit tests/unit/test_main.c tests/unit/test_parse.c
                         tests/unit/test_config.c tests/unit/test_modules.c)
target_include_directories(test_unit PRIVATE tests/unit)
target_link_libraries(test_unit PRIVATE sg)

include(GNUInstallDirs)
install(TARGETS sysglance RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES man/sysglance.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

enable_testing()
add_test(NAME unit COMMAND test_unit ${CMAKE_SOURCE_DIR}/tests/fixtures)
add_test(NAME integration
         COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/run.sh $<TARGET_FILE:sysglance>)
