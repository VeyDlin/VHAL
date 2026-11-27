# Host tests of the Memory module. Each Test*.cpp file is one executable, built with the Common and Periphery include roots.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_ROOT}/Periphery")
