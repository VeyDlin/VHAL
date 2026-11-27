# Host tests of the trigger. The shim replaces the umbrella header VHAL.h and comes first, then Periphery, then Common. The tests are
# built with optimisation and linked with the threads library.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  OPTIONS
    -O2
    -pthread
  LIBS
    -pthread)
