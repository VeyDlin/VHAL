# The ESP-AT client of the description, each object with one module set: the echo and row modules, the same with the await module, and
# neither module. The calls are the same in each, except that the rows call needs the row module, and the await calls are in the object
# with the await module only. The objects are compiled at -Os for Cortex-M4 and the size tool prints them. The script prints the
# VHAL_SKIP marker and passes when the ARM compiler is absent or older than GCC 14. The inputs are VHAL_ARM_CXX, VHAL_ARM_SIZE,
# VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

set(vhalSizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/../AtCompile.cmake")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(atArmVersion)
if(atArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the ESP-AT client size was not measured")
  return()
endif()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(atTargetFlags -std=gnu++23 -fno-rtti -fno-exceptions -Os -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror)
set(atFailures 0)

foreach(name IN ITEMS SizeEspAtRows SizeEspAtAwait SizeEspAtPlain)
  atCompileObject(objResult "${VHAL_ARM_CXX}" "${atTestDir}/${name}.cpp" "${VHAL_WORK_DIR}/${name}.o"
    ${atTargetFlags} ${atIncludeFlags} -I "${atTestDir}")
  if(NOT objResult STREQUAL "ok")
    message("SIZE OBJECT FAILED: target ${name}")
    math(EXPR atFailures "${atFailures} + 1")
  endif()
endforeach()

if(atFailures GREATER 0)
  message(FATAL_ERROR "${atFailures} ESP-AT object(s) did not build")
endif()

message("ESP-AT client, -Os for Cortex-M4 (text, data, bss): echo and rows; the same with await; neither module:")
execute_process(
  COMMAND "${VHAL_ARM_SIZE}"
    "${VHAL_WORK_DIR}/SizeEspAtRows.o"
    "${VHAL_WORK_DIR}/SizeEspAtAwait.o"
    "${VHAL_WORK_DIR}/SizeEspAtPlain.o"
  RESULT_VARIABLE sizeResult
  OUTPUT_VARIABLE sizeOut
  ERROR_QUIET)
if(NOT sizeResult EQUAL 0)
  message(FATAL_ERROR "SIZE FAILED: ${VHAL_ARM_SIZE}")
endif()
message("${sizeOut}")
