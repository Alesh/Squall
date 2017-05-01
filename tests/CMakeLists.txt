cmake_minimum_required(VERSION 3.2)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/..)

include(Default)
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cxx")

add_executable(catch main.cpp ${SOURCES})
target_link_libraries(catch ${LIBEV_LIBRARY})

enable_testing()
add_test(NAME catch_tests COMMAND catch)
add_custom_command(TARGET catch POST_BUILD COMMAND ctest --output-on-failure)
