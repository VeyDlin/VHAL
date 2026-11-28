# Host tests of the Console: the output of the numbers, the floats and the timestamp, and the reads. The shared shim of the frame
# provides the System clock that the timestamp reads.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_TEST_DIR}/shim"
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
