# Host tests of JWT: a token built from fixed claims matches a golden token, and the golden token verifies and yields its claims.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
