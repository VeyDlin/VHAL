# Host tests of the SHA-1 and SHA-256 hashes.

vhal_declared(Test*.cpp TestSha1.cpp TestSha256.cpp)

vhal_test(TestSha1.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery")

vhal_test(TestSha256.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery")
