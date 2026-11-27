# Host tests of the timer helper. The shim replaces the umbrella header VHAL.h and comes first, then the timer folder, then Periphery,
# then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    ".."
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
