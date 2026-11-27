# Host tests of the FFT: the tests and the demo, built with the threads library, and the builder error snippets, which must fail with
# their messages.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
  OPTIONS
    -pthread
  LIBS
    -pthread)

vhal_test(
  GLOB Demo*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
  OPTIONS
    -pthread
  LIBS
    -pthread)

vhal_fail(
  BuilderErrors
  INCLUDES
    "${VHAL_ROOT}/Common")
