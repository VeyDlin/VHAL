# Host tests of the WebSocket client. TestFrame runs the client against a scripted socket and runs everywhere. TestLive runs it over
# TCP against ws_server.py, which the test starts itself; it prints VHAL_SKIP: when Python 3, the websockets package or the POSIX
# socket headers are missing.

vhal_declared(Test*.cpp TestFrame.cpp TestLive.cpp)

vhal_test(TestFrame.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery")

vhal_test(TestLive.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery")

set_tests_properties("${VHAL_MODULE_LABEL}.TestLive" PROPERTIES SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")
