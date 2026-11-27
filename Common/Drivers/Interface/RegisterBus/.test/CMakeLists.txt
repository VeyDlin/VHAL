# Host tests of the register bus layer, built with -Wconversion as well. The header check compiles each header of the register bus
# folder alone, and the builder error snippets must fail with their messages.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
    "."
  OPTIONS
    -Wconversion)

vhal_check(HeaderAlone.cmake)

vhal_fail(
  BuilderErrors
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
