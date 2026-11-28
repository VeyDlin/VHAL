# Host tests of Base64: the round trips, the known vectors, and the refusal of invalid input.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
