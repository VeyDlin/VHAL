# Host tests of the EEPROM driver. The shim replaces the umbrella header VHAL.h and comes first, then Periphery, then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
