# Host tests of the complementary filter. RejectAngleRange.cpp must not compile, and the scripted check reads its diagnostic.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common")

vhal_check(RejectAngleRange.cmake)
