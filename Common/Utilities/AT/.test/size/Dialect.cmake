# The flash cost of the dialect. DialectNone.cpp and DialectAll.cpp, the same commands with no dialect item and with every dialect item,
# are compiled at -Os for Cortex-M4, and the size tool prints both objects. The difference is the dialect's code alone. The script
# prints the VHAL_SKIP marker and passes when the ARM compiler is absent or older than GCC 14, or the size tool is absent. The inputs are VHAL_ARM_CXX, VHAL_ARM_SIZE,
# VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

set(vhalSizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/../AtCompile.cmake")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(atArmVersion)
if(atArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the dialect size was not measured")
  return()
endif()

execute_process(
  COMMAND "${VHAL_ARM_SIZE}" --version
  RESULT_VARIABLE sizeProbe
  OUTPUT_QUIET
  ERROR_QUIET)
if(NOT sizeProbe EQUAL 0)
  message("VHAL_SKIP: ${VHAL_ARM_SIZE} is absent: the dialect size was not measured")
  return()
endif()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(atTargetFlags -std=gnu++23 -fno-rtti -fno-exceptions -Os -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror)
set(atFailures 0)

foreach(name IN ITEMS DialectNone DialectAll)
  atCompileObject(objResult "${VHAL_ARM_CXX}" "${atTestDir}/${name}.cpp" "${VHAL_WORK_DIR}/${name}.o"
    ${atTargetFlags} ${atIncludeFlags})
  if(NOT objResult STREQUAL "ok")
    message("DIALECT SET FAILED: target ${name}")
    math(EXPR atFailures "${atFailures} + 1")
  endif()
endforeach()

if(atFailures GREATER 0)
  message(FATAL_ERROR "${atFailures} dialect object(s) did not build")
endif()

message("dialect flash, -Os for Cortex-M4, object text size (no dialect item, every dialect item):")
execute_process(
  COMMAND "${VHAL_ARM_SIZE}" "${VHAL_WORK_DIR}/DialectNone.o" "${VHAL_WORK_DIR}/DialectAll.o"
  RESULT_VARIABLE sizeResult
  OUTPUT_VARIABLE sizeOut
  ERROR_QUIET)
if(NOT sizeResult EQUAL 0)
  message(FATAL_ERROR "SIZE FAILED: ${VHAL_ARM_SIZE}")
endif()
message("${sizeOut}")
