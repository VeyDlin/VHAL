# Host tests of the ESP32 adapters. The ESP-IDF headers are replaced by the stubs in shim, so the port headers compile without the SDK.
# The include roots are this folder's shim, the adapter shim, Periphery and Common. The tests are optimised and use threads.

vhal_test(GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery/Adapter/.test/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  OPTIONS -O2 -pthread
  LIBS pthread)
