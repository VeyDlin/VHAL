# Host tests of the PID controller: the cyclic roll range and its dead zone, the integrator limit and a sampling frequency of zero.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common")
