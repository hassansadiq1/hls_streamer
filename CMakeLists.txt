cmake_minimum_required(VERSION 3.4)
project(main_app)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -g3")

######### SET CMAKE #########
set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
######### SET CMAKE END #########

file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/app_main.cpp")

find_package(PkgConfig)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0)
MESSAGE(STATUS "GST= ${GST_LIBRARIES}")
include_directories(${GST_INCLUDE_DIRS})
link_libraries(${GST_LIBRARIES})

FIND_PACKAGE(X11 REQUIRED)
MESSAGE(STATUS "X11= ${X11_LIBRARIES}")
link_libraries(${X11_LIBRARIES})

include_directories(/opt/nvidia/deepstream/deepstream/sources/includes)
set(CMAKE_PREFIX_PATH /opt/nvidia/deepstream/deepstream/lib)
link_directories(/opt/nvidia/deepstream/deepstream/lib)

add_executable(app_main ${SOURCES})
target_link_libraries(app_main ${libs_collection})
install(TARGETS app_main DESTINATION ${PROJECT_SOURCE_DIR}/../app)
