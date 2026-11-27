cmake_minimum_required(VERSION 3.22)

# vubcheck: the PC check of the VUB demo board (.demo/STM32G4, DemoVub). Build it with MinGW g++ on Windows, or g++ on Linux:
#   cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -B <build folder> <this folder>
#   cmake --build <build folder>
# It links only system libraries: winmm, winusb, setupapi, cfgmgr32 and hid on Windows, libusb-1.0 on Linux (the PC links of VUB::PC).

project(vubcheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The Common folder is the include root of the library headers. DemoProducts.h, the identities of the demo, is in the USB tool folder.
get_filename_component(VUBCHECK_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
get_filename_component(VUBCHECK_IDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../USB/.tool/common" ABSOLUTE)
# The core's Stack.h includes System/System.h, which is in the Periphery folder.
get_filename_component(VUBCHECK_PERIPHERY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../Periphery" ABSOLUTE)

# The system libraries of the build. A library that the linker does not find skips the build: the counterpart test reports the skip.
if(WIN32)
    set(VUBCHECK_LIBS winmm winusb setupapi cfgmgr32 hid)
else()
    set(VUBCHECK_LIBS usb-1.0)
endif()

include(CheckCXXSourceCompiles)

set(VUBCHECK_MISSING "")
foreach(VUBCHECK_LIB IN LISTS VUBCHECK_LIBS)
    set(CMAKE_REQUIRED_LIBRARIES ${VUBCHECK_LIB})
    check_cxx_source_compiles("int main() { return 0; }" VUBCHECK_LINKS_${VUBCHECK_LIB})
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT VUBCHECK_LINKS_${VUBCHECK_LIB})
        list(APPEND VUBCHECK_MISSING ${VUBCHECK_LIB})
    endif()
endforeach()

if(VUBCHECK_MISSING)
    message("VHAL_SKIP: the system libraries ${VUBCHECK_MISSING} are absent: vubcheck was not built")
    return()
endif()

add_executable(vubcheck src/main.cpp)

target_include_directories(vubcheck PRIVATE ${VUBCHECK_COMMON_DIR} ${VUBCHECK_IDS_DIR} ${VUBCHECK_PERIPHERY_DIR})
target_compile_options(vubcheck PRIVATE -Wall -Wextra)

target_link_libraries(vubcheck PRIVATE ${VUBCHECK_LIBS})

if(WIN32)
    # One executable with no DLLs of the toolchain beside it. winmm gives the 1 ms timer period of the run.
    target_link_options(vubcheck PRIVATE -static)
endif()

# The offline tests: the number parser, and the host loopback of the PC side against the demo's device side. The loopback runs on
# an in-memory line and needs no device.
set(VUBCHECK_DEMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../.demo/STM32G4/Application")

find_package(Threads REQUIRED)

enable_testing()

add_executable(vubcheckDecimalArgumentTest test/TestDecimalArgument.cpp)
target_compile_options(vubcheckDecimalArgumentTest PRIVATE -Wall -Wextra)
add_test(NAME vubcheckDecimalArgument COMMAND vubcheckDecimalArgumentTest)

add_executable(vubcheckLoopbackTest test/TestVubBoardLoopback.cpp)
target_include_directories(vubcheckLoopbackTest PRIVATE ${VUBCHECK_COMMON_DIR} ${VUBCHECK_PERIPHERY_DIR} "${VUBCHECK_DEMO_DIR}")
target_compile_options(vubcheckLoopbackTest PRIVATE -Wall -Wextra)
target_link_libraries(vubcheckLoopbackTest PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(vubcheckLoopbackTest PRIVATE winmm)
endif()

add_test(NAME vubcheckLoopbackEcho COMMAND vubcheckLoopbackTest echo 0 1 2 3)
add_test(NAME vubcheckLoopbackRequests COMMAND vubcheckLoopbackTest requests 0 1 2 3)
