# Host tests of the register map: the descriptors, the bus, the access functions, the set and the view, and the wiki examples. The
# builder error snippets of the rules are one test per snippet. The checks are that every header compiles alone and that an unused
# descriptor leaves no symbol at -Os.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  OPTIONS -Wconversion)

vhal_fail(
  BuilderErrors
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

file(GLOB registerMapHeaders "${VHAL_ROOT}/Common/Utilities/Data/RegisterMap/*.h")

vhal_headers(
  ${registerMapHeaders}
  INCLUDES
    "${VHAL_ROOT}/Common"
    "."
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
  OPTIONS
    -Wconversion)

vhal_check(CheckUnused.cmake)
