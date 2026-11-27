# Host tests of the RT9467 constants. The shim replaces the umbrella header VHAL.h and comes first, then the driver folder, then Common.

vhal_test(
  TestRT9467Encode.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Common/Drivers/Battery/RT9467"
    "${VHAL_ROOT}/Common")
