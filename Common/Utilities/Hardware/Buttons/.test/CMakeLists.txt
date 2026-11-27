# Host tests of the buttons. The module's own shim provides AGPIO on top of the real GPIOAdapter, so the pin polarity behaves as on the
# target. The shim comes first, then Periphery (for the adapters) and Common.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
