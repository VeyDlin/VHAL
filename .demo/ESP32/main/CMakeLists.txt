idf_component_register(
    SRCS
        "../main.cpp"
        "../BSP/BSP.cpp"
        "../Application/Application.cpp"
        "${VHAL_DIR}/Periphery/System/System.cpp"
        "${VHAL_DIR}/Periphery/Adapter/Port/ESP32/System.cpp"
    INCLUDE_DIRS
        "../BSP" "../Application"
        "${VHAL_DIR}/Periphery" "${VHAL_DIR}/Common"
    REQUIRES driver esp_timer esp_system esp_hw_support freertos
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-register -Wno-unused-parameter)

# VHAL's FreeRTOS wrapper uses the upstream, unprefixed header names.
idf_component_get_property(FREERTOS_DIR freertos COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${FREERTOS_DIR}/FreeRTOS-Kernel/include/freertos"
    "${FREERTOS_DIR}/FreeRTOS-Kernel-SMP/include/freertos"
    "${FREERTOS_DIR}/esp_additions/include/freertos"
)
