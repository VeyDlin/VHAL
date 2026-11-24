cmake_minimum_required(VERSION 3.22)

# vubcheck: the PC check of the VUB demo board (.demo/STM32G4, DemoVub). Build it with MinGW g++ on Windows, or g++ on Linux:
#   cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -B <build folder> <this folder>
#   cmake --build <build folder>
# It links only system libraries: winmm, winusb, setupapi, cfgmgr32 and hid on Windows, libusb-1.0 on Linux (the PC links of VUB::PC).

project(vubcheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The Common folder is the include root of the library headers. DemoProducts.h, the identities of the demo, is in the USB tool folder.
get_filename_component(VUBCHECK_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
get_filename_component(VUBCHECK_IDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../USB/.tool/common" ABSOLUTE)

add_executable(vubcheck src/main.cpp)

target_include_directories(vubcheck PRIVATE ${VUBCHECK_COMMON_DIR} ${VUBCHECK_IDS_DIR})
target_compile_options(vubcheck PRIVATE -Wall -Wextra)

if(WIN32)
    # One executable with no DLLs of the toolchain beside it. winmm gives the 1 ms timer period of the run.
    target_link_libraries(vubcheck PRIVATE winmm winusb setupapi cfgmgr32 hid)
    target_link_options(vubcheck PRIVATE -static)
else()
    target_link_libraries(vubcheck PRIVATE usb-1.0)
endif()
