# Host tests of the tick helpers: the wrap edges, the 64-bit forms, the millisecond split, and the examples of the wiki page. The header is
# checked alone, as its consumers include it. BuilderErrors holds the calls that mix a uint64 with another type, which do not compile.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common")

vhal_fail(BuilderErrors INCLUDES "${VHAL_ROOT}/Common")

vhal_headers(
  "${VHAL_ROOT}/Common/Utilities/Time/Ticks.h"
  INCLUDES
    "${VHAL_ROOT}/Common")

vhal_declared(Test*.cpp TestTicks.cpp TestTicksReadme.cpp)
