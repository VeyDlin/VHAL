# Host tests of the command line: the parser, the editing, the history, the completion, the help, the arguments, the modules and
# the long commands, with the builder error snippets of its rules.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  OPTIONS -pthread
  LIBS -pthread)

vhal_fail(
  BuilderErrors
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")
