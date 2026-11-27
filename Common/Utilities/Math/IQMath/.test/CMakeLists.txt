# Host tests of the IQ core, the builder error snippets (which must fail with their messages), and the object check, which needs the
# ARM compiler and is skipped without it.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common")

vhal_fail(
  BuilderErrors
  INCLUDES
    "${VHAL_ROOT}/Common")

vhal_check(ObjectCheck.cmake)
