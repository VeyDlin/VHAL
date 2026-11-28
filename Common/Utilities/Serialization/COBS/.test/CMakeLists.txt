# Host tests of the COBS class template: the payload lengths around the block limits, escaped bytes, the round trip, a custom
# configuration, the encoder's output bound and the decoder's rejections. The shim replaces the umbrella header VHAL.h and comes
# first, then Periphery (for the System declaration), then Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
