# Host tests of the JSON writer, parser and lookups: the escapes, the number tokens, the rollback on overflow, and the nested documents.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
