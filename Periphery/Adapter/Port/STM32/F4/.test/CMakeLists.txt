# Host tests of the STM32 F4 adapters, built with optimisation and linked with the threads library. The shim replaces the umbrella
# header VHAL.h and the peripheral headers, and comes first, then Periphery, then Common.

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
