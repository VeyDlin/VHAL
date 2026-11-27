# Host tests of the ring buffer. The shim replaces the umbrella header VHAL.h and comes first, then Periphery, then Common, in the
# order the tests were always built with. TestRingBufferStorage checks a static object of a second translation unit.

vhal_test(
  TestRingBuffer.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_test(
  TestRingBufferStorage.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  SOURCES
    RingSecondUnit.cpp)

vhal_test(
  TestRingBufferModel.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_test(
  TestRingBufferReadme.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_declared(Test*.cpp TestRingBuffer.cpp TestRingBufferStorage.cpp TestRingBufferModel.cpp TestRingBufferReadme.cpp)
