# Host tests of the MPU6886 driver against a simulated I2C device. The shim replaces the umbrella header VHAL.h (the simulated bus; it
# includes the shared shim for the System functions) and comes first, then Periphery (for the System declaration), then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
