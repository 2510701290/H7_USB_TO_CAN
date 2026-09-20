# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(H7_Work)

target_sources(app PRIVATE src/main.cpp src/USB.cpp src/K_RTT_Thread.cpp src/K_CAN_Thread.cpp)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_source_files_properties(
    src/main.cpp
    PROPERTIES
    COMPILE_OPTIONS "-O0;-g3"
)