# Host tests of the tick helpers: the wrap edges, the 64-bit forms, the millisecond split, and the examples of the wiki page. The header is
# checked alone, as its consumers include it.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common")

vhal_headers(
  "${VHAL_ROOT}/Common/Utilities/Time/Ticks.h"
  INCLUDES
    "${VHAL_ROOT}/Common")

vhal_declared(Test*.cpp TestTicks.cpp TestTicksReadme.cpp)
