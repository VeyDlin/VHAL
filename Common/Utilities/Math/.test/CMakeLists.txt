# Host tests of the math helpers that are not part of a sub-module (interpolation and random numbers). Each Test*.cpp file is one
# executable, built with the Common include root.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common")
