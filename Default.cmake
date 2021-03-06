include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libev DEFAULT_MSG LIBEV_LIBRARY LIBEV_INCLUDE_DIR)

find_library(LIBEV_LIBRARY NAMES ev)
find_path(LIBEV_INCLUDE_DIR ev.h PATH_SUFFIXES include/ev)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11")
include_directories(${CMAKE_HOME_DIRECTORY}/include)

include(CheckIncludeFile)
check_include_file("unistd.h" UNISTD_H)
if("${UNISTD_H}" STREQUAL "")
    message(WARNING "unistd.h Not found; class EventBuffer being abstract" )
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_UNISTD_H")
endif()