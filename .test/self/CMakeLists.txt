# Self-test of the frame's check header and shared shim. TestFrameChecks covers every macro and accepted type, and the shim's clock
# and critical-section count. TestFrameFailure makes two checks fail and passes when Finish reports them and returns 1.
# The shim folder comes first, so <VHAL.h> is the shared shim and not the umbrella header of the library.

vhal_test(
  TestFrameChecks.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_test(TestFrameFailure.cpp)
