# Host tests of the battery level: the curve of a profile, the monotonic fall, construction in a loop and the empty profile.
# The shim replaces the umbrella header VHAL.h and comes first, then Periphery (for the System declaration), then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
