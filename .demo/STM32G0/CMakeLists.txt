cmake_minimum_required(VERSION 3.22)

# --- Project ---
set(VHAL_PROJECT_NAME STM32G0-Demo)                # project and output .elf file name

project(${VHAL_PROJECT_NAME} LANGUAGES C CXX ASM)

# --- VHAL ---
set(VHAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)    # path to VHAL (submodule or folder)
set(VHAL_PORT STM32)
set(VHAL_SERIES G0)                                # STM32 series (G0, F4, G4, H7...)
set(VHAL_CPU cortex-m0plus)                        # (donor) cmake/stm32cubemx/CMakeLists.txt → cpu_PARAMS

# --- Chip / Board ---
set(VHAL_LINKER_SCRIPT STM32G071XX_FLASH.ld)       # (donor) root or cmake/ → .ld file
set(VHAL_STARTUP startup_stm32g071xx.s)            # (donor) root or Core/Src/ → startup_stm32XXXX.s
set(VHAL_SYSTEM  system_stm32g0xx.c)               # (donor) Core/Src/ → system_stm32XXxx.c
set(VHAL_FREERTOS_PORT ARM_CM0)                    # FreeRTOS port for your core (see tables in README), remove if no RTOS
set(VHAL_FREERTOS_HEAP heap_4)                     # heap strategy (see tables in README), default: heap_4

# --- Defines ---
# (donor) cmake/stm32cubemx/CMakeLists.txt → target_compile_definitions
# or (donor) Core/Inc/main.h → #define
add_compile_definitions(
    STM32G071xx                                    # ← replace with your chip
    USE_FULL_LL_DRIVER
    HSE_VALUE=8000000
    HSE_STARTUP_TIMEOUT=100
    LSE_STARTUP_TIMEOUT=5000
    LSE_VALUE=32768
    EXTERNAL_CLOCK_VALUE=48000
    HSI_VALUE=16000000
    LSI_VALUE=32000
    VDD_VALUE=3300
    PREFETCH_ENABLE=0
    INSTRUCTION_CACHE_ENABLE=1
    DATA_CACHE_ENABLE=1
)

# --- CPU flags ---
set(VHAL_CPU_FLAGS "-mcpu=${VHAL_CPU} -mthumb")
# For Cortex-M4/M7 with FPU:
# set(VHAL_CPU_FLAGS "-mcpu=${VHAL_CPU} -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb")

include(${VHAL_DIR}/vhal.cmake)

# --- Target ---
vhal_target(${PROJECT_NAME}
    SOURCES
        main.cpp
        BSP
        Application
    INCLUDES
        BSP
        Application
)
