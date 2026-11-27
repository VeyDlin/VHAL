# Host tests of the MPU6886 driver against a simulated I2C device. The shim replaces the umbrella header VHAL.h (the bus and the delay)
# and comes first, then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Common")
