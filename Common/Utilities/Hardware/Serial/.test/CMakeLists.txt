# Host tests of the serial receive buffer. The shim replaces the umbrella header VHAL.h and comes first, then Periphery (for the
# System declaration that the ring buffer includes), then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
