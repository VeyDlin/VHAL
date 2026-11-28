# Host tests of the plant simulation: the integrator, the noise, each block against its formula, the chain rules, the open-loop
# scenarios and the wiki examples. The shim replaces the umbrella header VHAL.h, which the Park and Clarke transforms include.
# The compile-fail snippets are one test per file, and the header test compiles each header alone.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    shim
    "${VHAL_ROOT}/Common"
  OPTIONS -Wconversion)

vhal_fail(
  BuilderErrors
  INCLUDES
    shim
    "${VHAL_ROOT}/Common")

file(GLOB simulationHeaders "${VHAL_ROOT}/Common/Utilities/Math/Simulation/*.h" "${VHAL_ROOT}/Common/Utilities/Math/Simulation/*/*.h")

vhal_headers(
  ${simulationHeaders}
  INCLUDES
    shim
    "${VHAL_ROOT}/Common"
  OPTIONS
    -Wconversion)
