# Host tests of the compact streaming protocol. The protocol header includes VHAL.h, so the shared shim comes first; it supplies
# the clock, and System/System.h comes from Periphery.

vhal_test(
  TestCompactStreaming.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_declared(Test*.cpp TestCompactStreaming.cpp)
