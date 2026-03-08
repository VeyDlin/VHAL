# VHAL CMake module
# Usage:
#   set(VHAL_DIR path/to/VHAL)
#   set(VHAL_PORT "STM32")  # or "ENS", "ESP32"
#   set(VHAL_CPU_FLAGS "-mcpu=cortex-m0plus -mthumb")
#   set(PORT_DIR ${VHAL_DIR}/Periphery/Adapter/Port/STM32/G0)
#
#   # STM32 optional:
#   set(VHAL_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32G071XX_FLASH.ld)
#   set(VHAL_STARTUP startup_stm32g071xx.s)
#   set(VHAL_SYSTEM  system_stm32g0xx.c)
#   set(VHAL_FREERTOS_PORT ARM_CM0)   # omit if no FreeRTOS
#   set(VHAL_FREERTOS_HEAP heap_4)    # default: heap_4
#
#   include(${VHAL_DIR}/vhal.cmake)
#   add_compile_definitions(...)
#   vhal_target(MyApp SOURCES main.cpp BSP Application INCLUDES BSP Application)

# --- Defaults ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED VHAL_DIR)
    message(FATAL_ERROR "VHAL_DIR is not set. Set it before including vhal.cmake")
endif()

if(NOT DEFINED VHAL_PORT)
    message(FATAL_ERROR "VHAL_PORT is not set (e.g. STM32, ENS, ESP32)")
endif()

# Derive PORT_DIR from VHAL_PORT + VHAL_SERIES (if not set manually)
if(NOT DEFINED PORT_DIR)
    if(DEFINED VHAL_SERIES)
        set(PORT_DIR ${VHAL_DIR}/Periphery/Adapter/Port/${VHAL_PORT}/${VHAL_SERIES})
    else()
        set(PORT_DIR ${VHAL_DIR}/Periphery/Adapter/Port/${VHAL_PORT})
    endif()
endif()

# --- Build type ---
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
message(STATUS "VHAL: port=${VHAL_PORT}, build=${CMAKE_BUILD_TYPE}")

# --- Compiler flags ---
set(_VHAL_BASE_C_FLAGS "${VHAL_CPU_FLAGS} -fdata-sections -ffunction-sections -Wall")
set(_VHAL_BASE_CXX_FLAGS "${_VHAL_BASE_C_FLAGS} -fno-rtti -fno-exceptions -Wno-register")

set(CMAKE_C_FLAGS   "${_VHAL_BASE_C_FLAGS}"   CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${_VHAL_BASE_CXX_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "${VHAL_CPU_FLAGS} -x assembler-with-cpp -MMD -MP" CACHE STRING "" FORCE)


# --- Linker flags (if VHAL_LINKER_SCRIPT is set) ---
if(DEFINED VHAL_LINKER_SCRIPT)
    # Allow just filename — resolve to CMAKE_CURRENT_SOURCE_DIR
    if(NOT IS_ABSOLUTE ${VHAL_LINKER_SCRIPT})
        set(VHAL_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/${VHAL_LINKER_SCRIPT})
    endif()
    set(CMAKE_EXE_LINKER_FLAGS
        "${VHAL_CPU_FLAGS} -T${VHAL_LINKER_SCRIPT} -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs -Wl,-Map=${PROJECT_NAME}.map -Wl,--print-memory-usage"
        CACHE STRING "" FORCE
    )
endif()

# --- VHAL include directories ---
set(VHAL_INCLUDE_DIRS
    ${VHAL_DIR}/Periphery
    ${VHAL_DIR}/Common
)

# --- STM32 driver sources & includes (auto-collected from PORT_DIR) ---
set(_VHAL_DRIVER_SOURCES "")
set(_VHAL_DRIVER_INCLUDES "")

if(DEFINED PORT_DIR AND EXISTS ${PORT_DIR}/Drivers)
    # LL drivers
    file(GLOB _LL_SOURCES ${PORT_DIR}/Drivers/HAL/Src/*_ll_*.c)
    list(FILTER _LL_SOURCES EXCLUDE REGEX "ll_usb\\.c$")
    list(APPEND _VHAL_DRIVER_SOURCES ${_LL_SOURCES})

    # CMSIS / HAL includes
    foreach(_INC Core/Include Device/Include HAL/Inc)
        set(_PATH ${PORT_DIR}/Drivers/CMSIS/${_INC})
        if(NOT EXISTS ${_PATH})
            set(_PATH ${PORT_DIR}/Drivers/${_INC})
        endif()
        if(EXISTS ${_PATH})
            list(APPEND _VHAL_DRIVER_INCLUDES ${_PATH})
        endif()
    endforeach()

    # Startup
    if(DEFINED VHAL_STARTUP)
        set(_STARTUP_PATH ${PORT_DIR}/Drivers/CMSIS/Device/Source/Templates/gcc/${VHAL_STARTUP})
        if(EXISTS ${_STARTUP_PATH})
            list(APPEND _VHAL_DRIVER_SOURCES ${_STARTUP_PATH})
        else()
            message(WARNING "VHAL: startup file not found: ${_STARTUP_PATH}")
        endif()
    endif()

    # System
    if(DEFINED VHAL_SYSTEM)
        set(_SYSTEM_PATH ${PORT_DIR}/Drivers/CMSIS/Device/Source/Templates/${VHAL_SYSTEM})
        if(EXISTS ${_SYSTEM_PATH})
            list(APPEND _VHAL_DRIVER_SOURCES ${_SYSTEM_PATH})
        else()
            message(WARNING "VHAL: system file not found: ${_SYSTEM_PATH}")
        endif()
    endif()
endif()

# --- FreeRTOS (if VHAL_FREERTOS_PORT is set) ---
set(_VHAL_FREERTOS_SOURCES "")
set(_VHAL_FREERTOS_INCLUDES "")

if(DEFINED VHAL_FREERTOS_PORT)
    set(_FREERTOS_DIR ${VHAL_DIR}/Periphery/OS/Port/FreeRTOS/Kernel)

    if(NOT DEFINED VHAL_FREERTOS_HEAP)
        set(VHAL_FREERTOS_HEAP heap_4)
    endif()

    set(_VHAL_FREERTOS_SOURCES
        ${_FREERTOS_DIR}/tasks.c
        ${_FREERTOS_DIR}/queue.c
        ${_FREERTOS_DIR}/list.c
        ${_FREERTOS_DIR}/timers.c
        ${_FREERTOS_DIR}/event_groups.c
        ${_FREERTOS_DIR}/stream_buffer.c
        ${_FREERTOS_DIR}/portable/GCC/${VHAL_FREERTOS_PORT}/port.c
        ${_FREERTOS_DIR}/portable/MemMang/${VHAL_FREERTOS_HEAP}.c
    )

    set(_VHAL_FREERTOS_INCLUDES
        ${_FREERTOS_DIR}/include
        ${_FREERTOS_DIR}/portable/GCC/${VHAL_FREERTOS_PORT}
    )
endif()

# --- VHAL system sources ---
set(VHAL_SOURCES
    ${VHAL_DIR}/Periphery/System/System.cpp
    ${VHAL_DIR}/Periphery/Calls.cpp
)
set(_PORT_SYSTEM ${VHAL_DIR}/Periphery/Adapter/Port/${VHAL_PORT}/System.cpp)
if(EXISTS ${_PORT_SYSTEM})
    list(APPEND VHAL_SOURCES ${_PORT_SYSTEM})
endif()

# --- Post-build: .bin, .hex, size ---
function(vhal_post_build TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
        COMMENT "Generating .bin / .hex and printing size"
    )
    set_target_properties(${TARGET_NAME} PROPERTIES
        ADDITIONAL_CLEAN_FILES "${TARGET_NAME}.map;${TARGET_NAME}.bin;${TARGET_NAME}.hex"
    )
endfunction()

# --- Collect sources from directories ---
# vhal_collect_sources(OUT_VAR dir1 dir2 ...)
# Recursively collects *.cpp *.c *.s from listed directories (or individual files)
function(vhal_collect_sources OUT_VAR)
    set(_ALL "")
    foreach(_ENTRY ${ARGN})
        if(IS_ABSOLUTE ${_ENTRY})
            set(_DIR ${_ENTRY})
        else()
            set(_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${_ENTRY})
        endif()
        if(IS_DIRECTORY ${_DIR})
            file(GLOB_RECURSE _FOUND
                ${_DIR}/*.cpp
                ${_DIR}/*.c
                ${_DIR}/*.s
            )
            list(APPEND _ALL ${_FOUND})
        else()
            list(APPEND _ALL ${_ENTRY})
        endif()
    endforeach()
    set(${OUT_VAR} ${_ALL} PARENT_SCOPE)
endfunction()

# --- Collect include dirs from directories ---
# vhal_collect_includes(OUT_VAR dir1 dir2 ...)
# Recursively finds all directories containing *.h files
function(vhal_collect_includes OUT_VAR)
    set(_ALL "")
    foreach(_DIR ${ARGN})
        list(APPEND _ALL ${_DIR})
        file(GLOB_RECURSE _HEADERS ${_DIR}/*.h)
        foreach(_H ${_HEADERS})
            get_filename_component(_HDIR ${_H} DIRECTORY)
            list(APPEND _ALL ${_HDIR})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _ALL)
    set(${OUT_VAR} ${_ALL} PARENT_SCOPE)
endfunction()

# --- Main function: create target with VHAL ---
# vhal_target(MyApp SOURCES src_dir1 src_dir2 ... INCLUDES inc_dir1 inc_dir2 ...)
function(vhal_target TARGET_NAME)
    cmake_parse_arguments(_VT "" "" "SOURCES;INCLUDES" ${ARGN})

    # Collect sources from directories
    vhal_collect_sources(_USER_SOURCES ${_VT_SOURCES})

    # Collect includes from directories
    vhal_collect_includes(_USER_INCLUDES ${_VT_INCLUDES})

    add_executable(${TARGET_NAME}
        ${_USER_SOURCES}
        ${VHAL_SOURCES}
        ${_VHAL_DRIVER_SOURCES}
        ${_VHAL_FREERTOS_SOURCES}
    )
    target_include_directories(${TARGET_NAME} PRIVATE
        ${VHAL_INCLUDE_DIRS}
        ${_USER_INCLUDES}
        ${_VHAL_DRIVER_INCLUDES}
        ${_VHAL_FREERTOS_INCLUDES}
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
    )
    vhal_post_build(${TARGET_NAME})
endfunction()
