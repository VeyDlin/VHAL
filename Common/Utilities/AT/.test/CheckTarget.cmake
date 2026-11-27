# Checks the scale set of the AT description with the ARM compiler: a syntax check with -fno-rtti -fno-exceptions. The script prints
# the VHAL_SKIP marker and passes when the ARM compiler is absent or older than GCC 14. The inputs are VHAL_ARM_CXX (the ARM compiler),
# VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

include("${CMAKE_CURRENT_LIST_DIR}/AtCompile.cmake")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(atArmVersion)
if(atArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the target check of the scale set was not run")
  return()
endif()

message("target compiler: ${VHAL_ARM_CXX} ${atArmVersion}")

execute_process(
  COMMAND "${VHAL_ARM_CXX}" -std=gnu++23 -fno-rtti -fno-exceptions -Wall -Wextra -Werror -fsyntax-only
    ${atIncludeFlags} "${atTestDir}/ScaleSet.cpp"
  RESULT_VARIABLE scaleResult
  ERROR_VARIABLE scaleErr
  OUTPUT_QUIET)

if(scaleResult EQUAL 0)
  message("scale set passes the target syntax check")
else()
  message("${scaleErr}")
  message(FATAL_ERROR "SCALE SET FAILED: target")
endif()
