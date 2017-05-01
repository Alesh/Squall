cmake_minimum_required(VERSION 3.2)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/..)
project(Squall_CXX_Demo)

include(Default)
include(FindPackageHandleStandardArgs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(hello_cb hello_cb.cxx)
target_link_libraries(hello_cb ${LIBEV_LIBRARY})

add_executable(hello_cp hello_cp.cxx)
target_link_libraries(hello_cp ${LIBEV_LIBRARY})
