cmake_minimum_required(VERSION 3.22)

# --- Project ---
set(VHAL_PROJECT_NAME ENS001)

project(${VHAL_PROJECT_NAME} LANGUAGES C CXX ASM)

# --- VHAL ---
set(VHAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(VHAL_PORT ENS)
set(VHAL_CPU cortex-m0)
set(PORT_DIR ${VHAL_DIR}/Periphery/Adapter/Port/ENS/001)

# --- Chip / Board ---
set(VHAL_LINKER_SCRIPT ${PORT_DIR}/Linker/CM0.ld)

# --- CPU flags ---
set(VHAL_CPU_FLAGS "-mcpu=${VHAL_CPU} -mthumb")

include(${VHAL_DIR}/vhal.cmake)

# --- Linker (ENS uses additional -L for lib scripts) ---
set(CMAKE_EXE_LINKER_FLAGS
    "${VHAL_CPU_FLAGS} -T${VHAL_LINKER_SCRIPT} -L${PORT_DIR}/Linker -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs -Wl,-Map=${PROJECT_NAME}.map -Wl,--print-memory-usage"
    CACHE STRING "" FORCE
)

# --- Target ---
vhal_target(${PROJECT_NAME}
    SOURCES
        main.cpp
        BSP
        Application
        ${PORT_DIR}/Drivers/Device
        ${PORT_DIR}/Drivers/Startup/GCC
    INCLUDES
        BSP
        Application
        ${PORT_DIR}/Drivers/Include
        ${PORT_DIR}/Drivers/Device
)
