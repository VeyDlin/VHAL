# Host tests of the software I2C bus. The shim replaces the umbrella header VHAL.h and comes first, then Periphery, then Common, then
# the I2C folder itself.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
    "..")
