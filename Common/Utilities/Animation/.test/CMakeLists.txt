# Host tests of the animation: the tween, the spring and the RTOS wrapper. The shim replaces the umbrella header VHAL.h and comes first,
# then Common. RejectSpringRange.cpp must not compile, and the scripted check reads its diagnostic.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Common")

vhal_check(RejectSpringRange.cmake)
