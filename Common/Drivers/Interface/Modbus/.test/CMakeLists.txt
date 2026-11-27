# Host tests of the Modbus layers, the header check, the builder error snippets and the size measurements of the probes. The shim
# replaces the umbrella header VHAL.h and comes first, then Periphery, then Common, then this folder.
# The default run has no sanitizers. For a sanitizer run, delete .test-build and configure it again with CXXFLAGS set, for example
# CXXFLAGS="-fsanitize=address,undefined" ./test.cmd Modbus. The flags stay in .test-build until it is deleted, and the compiler must
# have the sanitizer runtime (the MinGW toolchain on Windows does not, so configure fails there).

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common"
    ".")

vhal_check(HeaderAlone.cmake)

vhal_fail(
  BuilderErrors
  INCLUDES
    shim
    "${VHAL_ROOT}/Periphery"
    "${VHAL_ROOT}/Common")

vhal_size(size/size.cmake)
