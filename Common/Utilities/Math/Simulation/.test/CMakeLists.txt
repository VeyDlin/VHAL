# Host tests of the plant simulation: the integrator, the noise, each block against its formula, the chain rules, the open-loop
# scenarios and the wiki examples. The compile-fail snippets are one test per file, and the header test compiles each header alone.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common"
  OPTIONS -Wconversion)

vhal_fail(
  BuilderErrors
  INCLUDES
    "${VHAL_ROOT}/Common")

file(GLOB simulationHeaders "${VHAL_ROOT}/Common/Utilities/Math/Simulation/*.h" "${VHAL_ROOT}/Common/Utilities/Math/Simulation/*/*.h")

vhal_headers(
  ${simulationHeaders}
  INCLUDES
    "${VHAL_ROOT}/Common"
  OPTIONS
    -Wconversion)
