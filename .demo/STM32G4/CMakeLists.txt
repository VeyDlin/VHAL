cmake_minimum_required(VERSION 3.22)

# --- Project ---
set(VHAL_PROJECT_NAME USBBenchG431)                # project and output .elf / .bin file name

project(${VHAL_PROJECT_NAME} LANGUAGES C CXX ASM)

# --- VHAL ---
# This folder is .demo/STM32G4, so VHAL is two levels up.
get_filename_component(VHAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(VHAL_PORT STM32)
set(VHAL_SERIES G4)

# The benchmark protocol (BenchProtocol.h) is shared with the PC tool usbbench, so it stays in the USB tool folder and the
# demo includes it from there.
get_filename_component(BENCH_COMMON_DIR "${VHAL_DIR}/Common/Drivers/Interface/USB/.tool/common" ABSOLUTE)

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

include(${VHAL_DIR}/vhal.cmake)

# --- Target ---
# BSP and Application are collected recursively, so each demo folder under Application/ (DemoUSBBench/ now) is compiled and
# on the include path without being listed here. Add a demo by adding its folder, and choose it in Application/Application.cpp.
vhal_target(${PROJECT_NAME}
    SOURCES
        main.cpp
        BSP
        Application
    INCLUDES
        BSP
        Application
        ${BENCH_COMMON_DIR}
)

# The data section is loaded from flash into RAM (AT> FLASH), which newer binutils report as an RWX load segment. The layout
# is the usual one for STM32 scripts and is correct, so the warning is silenced here.
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--no-warn-rwx-segments)
