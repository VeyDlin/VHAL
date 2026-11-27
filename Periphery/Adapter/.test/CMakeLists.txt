# Host tests of the adapter base classes. The tests are optimised: a state the optimiser keeps in a register makes Await time out
# instead of seeing the write of the interrupt, and TestAwaitEndedByAnotherThread fails. The shim folder replaces the umbrella header
# VHAL.h and comes first, then Periphery, then Common.

vhal_test(GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
  OPTIONS -O2 -pthread
  LIBS pthread)
