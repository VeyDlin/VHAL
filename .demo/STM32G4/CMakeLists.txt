cmake_minimum_required(VERSION 3.22)

# --- Demo selection ---
# Each folder Application/Demo<Name> is a demo, and VHAL_DEMO=<Name> builds one of them (USBBench by default). Only that folder,
# Application/Application.cpp and Application/Application.h are compiled, so a demo that is not chosen costs no flash and no RAM.
# The list of demos is the list of folders. A new demo needs its folder and a branch in Application/Application.cpp. CONFIGURE_DEPENDS
# makes a folder added or removed after the configure step show up at the next build, without a manual re-configure.
set(VHAL_DEMO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/Application)
file(GLOB VHAL_DEMO_FOLDERS RELATIVE ${VHAL_DEMO_ROOT} CONFIGURE_DEPENDS ${VHAL_DEMO_ROOT}/Demo*)
set(VHAL_DEMO_NAMES "")
foreach(VHAL_DEMO_FOLDER ${VHAL_DEMO_FOLDERS})
    if(IS_DIRECTORY ${VHAL_DEMO_ROOT}/${VHAL_DEMO_FOLDER})
        string(REGEX REPLACE "^Demo" "" VHAL_DEMO_ITEM ${VHAL_DEMO_FOLDER})
        list(APPEND VHAL_DEMO_NAMES ${VHAL_DEMO_ITEM})
    endif()
endforeach()
list(SORT VHAL_DEMO_NAMES)

# The cache keeps the last VHAL_DEMO of a build directory. Each demo gets its own build directory (-B), because switching VHAL_DEMO in
# one directory leaves the ELF and BIN of the previous demo behind next to the new ones.
set(VHAL_DEMO USBBench CACHE STRING "Demo to build: one of the names from the Application/Demo<Name> folders")
set_property(CACHE VHAL_DEMO PROPERTY STRINGS ${VHAL_DEMO_NAMES})
list(FIND VHAL_DEMO_NAMES "${VHAL_DEMO}" VHAL_DEMO_INDEX)
if(VHAL_DEMO_INDEX EQUAL -1)
    message(FATAL_ERROR "VHAL_DEMO=${VHAL_DEMO} is not a demo. Choose one of: ${VHAL_DEMO_NAMES}")
endif()

# --- Project ---
# The output is named after the demo: USBBenchG431.elf and .bin for the default demo.
set(VHAL_PROJECT_NAME ${VHAL_DEMO}G431)

project(${VHAL_PROJECT_NAME} LANGUAGES C CXX ASM)

# --- VHAL ---
# This folder is .demo/STM32G4, so VHAL is two levels up.
get_filename_component(VHAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(VHAL_PORT STM32)
set(VHAL_SERIES G4)

# The demo identities (DemoProducts.h) and the benchmark protocol (BenchProtocol.h) are shared with the PC tools, so they stay in the
# USB tool folder and the demos include them from there.
get_filename_component(TOOL_COMMON_DIR "${VHAL_DIR}/Common/Drivers/Interface/USB/.tool/common" ABSOLUTE)

# --- Chip / Board ---
set(VHAL_LINKER_SCRIPT STM32G431XB_FLASH.ld)
set(VHAL_STARTUP startup_stm32g431xx.s)
set(VHAL_SYSTEM  system_stm32g4xx.c)

# --- Defines ---
add_compile_definitions(
    STM32G431xx
    USE_FULL_LL_DRIVER
    HSE_VALUE=8000000
    HSE_STARTUP_TIMEOUT=100
    LSE_STARTUP_TIMEOUT=5000
    LSE_VALUE=32768
    EXTERNAL_CLOCK_VALUE=48000
    HSI_VALUE=16000000
    LSI_VALUE=32000
    VDD_VALUE=3300
)

# --- CPU flags: Cortex-M4 with the single-precision FPU of the G431 ---
set(VHAL_CPU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb")

# The demo chosen with VHAL_DEMO. Application.cpp picks its entry point with this define.
add_compile_definitions(VHAL_DEMO_${VHAL_DEMO})

# The mode of the VUB demo. Echo (the default) is the link check of the four stacks, and it is the image without any requests code.
# Requests is the requests demo: a simulated actuator on the UART and the CDC serial port (vubcheck --requests). The mode is a compile
# definition, so each image holds only its own code.
if(VHAL_DEMO STREQUAL "Vub")
    set(VHAL_VUB_MODE Echo CACHE STRING "Mode of the VUB demo: Echo (the link check) or Requests (the requests demo)")
    set_property(CACHE VHAL_VUB_MODE PROPERTY STRINGS Echo Requests)
    if(VHAL_VUB_MODE STREQUAL "Requests")
        add_compile_definitions(VHAL_VUB_REQUESTS)
    elseif(NOT VHAL_VUB_MODE STREQUAL "Echo")
        message(FATAL_ERROR "VHAL_VUB_MODE=${VHAL_VUB_MODE} is not a mode. Choose Echo or Requests")
    endif()
endif()

include(${VHAL_DIR}/vhal.cmake)

# --- Target ---
# Only the chosen demo folder is listed, so the other demos are neither compiled nor linked. Application.h and the demo's headers
# are found from Application/ (for example <DemoUSBBench/DemoUSBBench.h>), which is added below.
vhal_target(${PROJECT_NAME}
    SOURCES
        main.cpp
        BSP
        Application/Application.cpp
        Application/Demo${VHAL_DEMO}
    INCLUDES
        BSP
        ${TOOL_COMMON_DIR}
)
target_include_directories(${PROJECT_NAME} PRIVATE ${VHAL_DEMO_ROOT})

# The data section is loaded from flash into RAM (AT> FLASH), which newer binutils report as an RWX load segment. The layout
# is the usual one for STM32 scripts and is correct, so the warning is silenced here.
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--no-warn-rwx-segments)

# --- DFU demo: the flash region it writes ---
# The region is the top 32 KB of flash (16 pages of 2 KB). The demo writes it over DFU and the image must end below it: the linker
# checks that against VhalDfuRegionStart (STM32G431XB_FLASH.ld), so an image that grows into the region fails the build. The start and
# size are also compile definitions of the demo. Only the DFU demo gets them.
if(VHAL_DEMO STREQUAL "USBDfu")
    set(VHAL_DFU_REGION_START 0x08018000)
    set(VHAL_DFU_REGION_SIZE 0x8000)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VHAL_DFU_REGION_START=${VHAL_DFU_REGION_START}
        VHAL_DFU_REGION_SIZE=${VHAL_DFU_REGION_SIZE}
    )
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--defsym=VhalDfuRegionStart=${VHAL_DFU_REGION_START})
endif()
