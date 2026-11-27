# Measures the cost of the Modbus probes for a Cortex-M4 target. Each .cpp in this folder is one row of the size tables on the
# ModbusServer, ModbusClient and RtuTransport pages. Each probe is compiled with the ARM compiler to an object file, and the size tool
# reads its text, data and bss. The transport and the UART are declared and not defined, so an object holds only the code its probe
# reaches, and no link is needed. The script prints one line per probe, in bytes, and fails when a probe does not build. It prints the
# VHAL_SKIP marker and returns when the ARM compiler is absent or older than GCC 14.
cmake_minimum_required(VERSION 3.25)

set(sizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(armVersion)
if(armVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the Modbus probe sizes were not run")
  return()
endif()

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(probeFlags -std=gnu++23 -mcpu=cortex-m4 -mthumb -fno-rtti -fno-exceptions -Os -Wall -Wextra -Werror)
set(probeIncludes
  -I "${sizeDir}/shim"
  -I "${sizeDir}"
  -I "${VHAL_ROOT}/Common"
  -I "${VHAL_ROOT}/Periphery")


# Pads a value with spaces on the right to the width, for the name column.
function(sizePadRight resultVar value width)
  string(LENGTH "${value}" valueLength)
  math(EXPR padCount "${width} - ${valueLength}")
  set(padding "")
  if(padCount GREATER 0)
    string(REPEAT " " ${padCount} padding)
  endif()
  set(${resultVar} "${value}${padding}" PARENT_SCOPE)
endfunction()


# Pads a value with spaces on the left to the width, for the number columns.
function(sizePadLeft resultVar value width)
  string(LENGTH "${value}" valueLength)
  math(EXPR padCount "${width} - ${valueLength}")
  set(padding "")
  if(padCount GREATER 0)
    string(REPEAT " " ${padCount} padding)
  endif()
  set(${resultVar} "${padding}${value}" PARENT_SCOPE)
endfunction()


# Prints one row of the table: the name in 22 columns, then text, data and bss in 8 columns each.
function(sizeRow name text data bss)
  sizePadRight(nameCell "${name}" 22)
  sizePadLeft(textCell "${text}" 8)
  sizePadLeft(dataCell "${data}" 8)
  sizePadLeft(bssCell "${bss}" 8)
  message("${nameCell} ${textCell} ${dataCell} ${bssCell}")
endfunction()


sizeRow(probe text data bss)

file(GLOB probeSources "${sizeDir}/*.cpp")
list(SORT probeSources)

set(probeFailures 0)
foreach(source IN LISTS probeSources)
  get_filename_component(name "${source}" NAME_WE)
  set(object "${VHAL_WORK_DIR}/${name}.o")

  execute_process(
    COMMAND "${VHAL_ARM_CXX}" ${probeFlags} ${probeIncludes} -c -o "${object}" "${source}"
    RESULT_VARIABLE buildResult
    ERROR_VARIABLE buildErr
    OUTPUT_QUIET)
  if(NOT buildResult EQUAL 0)
    message("${buildErr}")
    message("BUILD FAILED: ${name}")
    math(EXPR probeFailures "${probeFailures} + 1")
    continue()
  endif()

  execute_process(
    COMMAND "${VHAL_ARM_SIZE}" --format=berkeley "${object}"
    RESULT_VARIABLE sizeResult
    OUTPUT_VARIABLE sizeOut
    ERROR_QUIET)
  if(NOT sizeResult EQUAL 0)
    message(FATAL_ERROR "${VHAL_ARM_SIZE} could not read ${object}")
  endif()

  string(REPLACE "\r" "" sizeOut "${sizeOut}")
  string(STRIP "${sizeOut}" sizeOut)
  string(REGEX MATCH "[^\n]*$" lastLine "${sizeOut}")
  string(STRIP "${lastLine}" lastLine)
  string(REGEX REPLACE "[ \t]+" ";" fields "${lastLine}")
  list(GET fields 0 text)
  list(GET fields 1 data)
  list(GET fields 2 bss)

  sizeRow("${name}" "${text}" "${data}" "${bss}")
endforeach()

if(probeFailures GREATER 0)
  message(FATAL_ERROR "${probeFailures} probe(s) failed to build")
endif()
