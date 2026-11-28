# Host tests of the filters: the complementary filter, the low-pass filter, the hysteresis window and the Kalman filter.
# The shim replaces the umbrella header VHAL.h and comes first, then Periphery (for the System declaration), then Common.
# RejectAngleRange.cpp must not compile, and the scripted check reads its diagnostic.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_check(RejectAngleRange.cmake)
