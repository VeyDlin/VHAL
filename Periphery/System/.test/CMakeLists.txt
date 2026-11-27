# Host tests of the system critical sections. The include roots are Periphery, then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
