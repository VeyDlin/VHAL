# Host tests of the field oriented control. The shim replaces the umbrella header VHAL.h and comes first, then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Common")
