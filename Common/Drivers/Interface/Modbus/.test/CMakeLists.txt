# Host tests of the Modbus layers, the header check, the builder error snippets and the size measurements of the probes. The shim
# replaces the umbrella header VHAL.h and comes first, then Periphery, then Common, then this folder.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
    ".")

vhal_check(HeaderAlone.cmake)

vhal_fail(
  BuilderErrors
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_size(size/size.cmake)
