# Host tests of HttpClient and ServerSentEvents. TestHttpUnit and TestSse run against scripted sockets and run everywhere.
# TestHttpIntegration runs the client over TCP against http_server.py, which the test starts itself; it prints VHAL_SKIP: when
# Python 3 or the POSIX socket headers are missing.

vhal_declared(Test*.cpp TestHttpUnit.cpp TestSse.cpp TestHttpIntegration.cpp)

vhal_test(TestHttpUnit.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery")

vhal_test(TestSse.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery")

vhal_test(TestHttpIntegration.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery")

set_tests_properties("${VHAL_MODULE_LABEL}.TestHttpIntegration" PROPERTIES SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")
