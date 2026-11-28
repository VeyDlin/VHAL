# Host tests of the bit helpers, the byte converter and the colour types. The register map has its own tests in RegisterMap/.test.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_check(RuntimeFailures/BitOutOfRange.cmake)

vhal_fail(
  ByteConverterErrors
  INCLUDES
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_fail(
  ByteConverterGnuErrors
  INCLUDES
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  OPTIONS -std=gnu++23)
