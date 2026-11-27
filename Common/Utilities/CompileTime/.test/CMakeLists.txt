# Host tests of the compile-time checks, the run-time failure of the host definitions, and the builder error snippets.

vhal_test(
  TestRequire.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    .)

vhal_check(RuntimeFailures/RuntimeFailure.cmake ARGS plain)
vhal_check(RuntimeFailures/RuntimeFailure.cmake ARGS value)

vhal_fail(BuilderErrors INCLUDES "${VHAL_ROOT}/Common")

vhal_declared(Test*.cpp TestRequire.cpp)
