cmake_minimum_required(VERSION 3.22)

# Use this demo in the VHAL checkout, or copy it beside a VHAL directory.
if(NOT DEFINED VHAL_DIR)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/VHAL/Periphery/VHAL.h")
        set(VHAL_DIR "${CMAKE_CURRENT_LIST_DIR}/VHAL")
    else()
        get_filename_component(VHAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    endif()
endif()
set(VHAL_DIR "${VHAL_DIR}" CACHE PATH "VHAL checkout directory")
if(NOT EXISTS "${VHAL_DIR}/Periphery/VHAL.h")
    message(FATAL_ERROR "Set VHAL_DIR to a VHAL checkout")
endif()

set(IDF_TARGET "esp32c3" CACHE STRING "ESP-IDF target")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(VHALDemo)
