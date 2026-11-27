# The client and the server of one five-command set, each compiled alone at -Os for Cortex-M4, so each object's text, data and bss is its
# own side's code. The size tool prints both objects. The script prints the VHAL_SKIP marker and passes when the ARM compiler is absent
# or older than GCC 14. The inputs are VHAL_ARM_CXX, VHAL_ARM_SIZE, VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

set(vhalSizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/../AtCompile.cmake")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(atArmVersion)
if(atArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the server and client sizes were not measured")
  return()
endif()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(atTargetFlags -std=gnu++23 -fno-rtti -fno-exceptions -Os -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror)
set(atFailures 0)

foreach(name IN ITEMS SizeServer SizeClient)
  atCompileObject(objResult "${VHAL_ARM_CXX}" "${atTestDir}/${name}.cpp" "${VHAL_WORK_DIR}/${name}.o"
    ${atTargetFlags} ${atIncludeFlags} -I "${atTestDir}")
  if(NOT objResult STREQUAL "ok")
    message("SIZE OBJECT FAILED: target ${name}")
    math(EXPR atFailures "${atFailures} + 1")
  endif()
endforeach()

if(atFailures GREATER 0)
  message(FATAL_ERROR "${atFailures} server or client object(s) did not build")
endif()

message("server and client of a five-command set, -Os for Cortex-M4 (text, data, bss):")
execute_process(
  COMMAND "${VHAL_ARM_SIZE}" "${VHAL_WORK_DIR}/SizeServer.o" "${VHAL_WORK_DIR}/SizeClient.o"
  RESULT_VARIABLE sizeResult
  OUTPUT_VARIABLE sizeOut
  ERROR_QUIET)
if(NOT sizeResult EQUAL 0)
  message(FATAL_ERROR "SIZE FAILED: ${VHAL_ARM_SIZE}")
endif()
message("${sizeOut}")
