# The object check of the IQ core: each sample in ObjectCheck/ is compiled to an object for Cortex-M0+ with -Os, and the check fails when
# the object references a soft-float or soft-double helper (__aeabi_f* or __aeabi_d*). The IQ types must not reach either helper. A
# control sample, control_*.cpp, must reference one, so the check is known to see the helpers. The script needs the ARM compiler and
# its nm tool, which is the compiler's neighbour; it prints the VHAL_SKIP marker and returns when either is absent.
# The inputs are VHAL_ARM_CXX (the ARM compiler), VHAL_ROOT (the repository root) and VHAL_WORK_DIR (a folder it may empty).
cmake_minimum_required(VERSION 3.25)

set(sampleDir "${CMAKE_CURRENT_LIST_DIR}/ObjectCheck")

# The nm tool is named after the compiler with the g++ suffix (and its version, when there is one) replaced by nm.
string(REGEX REPLACE "g\\+\\+(-[0-9.]+)?(\\.exe)?$" "nm\\2" armNm "${VHAL_ARM_CXX}")

execute_process(
  COMMAND "${VHAL_ARM_CXX}" --version
  RESULT_VARIABLE armResult
  OUTPUT_QUIET
  ERROR_QUIET)
execute_process(
  COMMAND "${armNm}" --version
  RESULT_VARIABLE nmResult
  OUTPUT_QUIET
  ERROR_QUIET)
execute_process(
  COMMAND "${VHAL_ARM_CXX}" -dumpfullversion
  RESULT_VARIABLE versionResult
  OUTPUT_VARIABLE armVersion
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT armResult EQUAL 0 OR NOT nmResult EQUAL 0)
  message("VHAL_SKIP: ${VHAL_ARM_CXX} or ${armNm} is absent: the IQ object check was not run")
  return()
endif()

# The object check needs GCC 14 or newer, the lowest supported compiler (as the size tests do).
set(armMajor "")
if(versionResult EQUAL 0 AND armVersion MATCHES "^[0-9]+")
  string(REGEX MATCH "^[0-9]+" armMajor "${armVersion}")
endif()
if(armMajor STREQUAL "" OR armMajor LESS 14)
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is older than GCC 14 or its version is unknown: the IQ object check was not run")
  return()
endif()

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

file(GLOB samples "${sampleDir}/*.cpp")
list(SORT samples)

set(failures 0)
foreach(source IN LISTS samples)
  get_filename_component(name "${source}" NAME_WE)
  set(object "${VHAL_WORK_DIR}/${name}.o")

  execute_process(
    COMMAND "${VHAL_ARM_CXX}" -std=c++23 -c -Os -mthumb -mcpu=cortex-m0plus -fno-exceptions -fno-rtti
      -ffunction-sections -fdata-sections
      -I "${VHAL_ROOT}/Common"
      -I "${sampleDir}/shim"
      -o "${object}" "${source}"
    RESULT_VARIABLE buildResult
    ERROR_VARIABLE buildErr
    OUTPUT_QUIET)
  if(NOT buildResult EQUAL 0)
    message("${buildErr}")
    message("BUILD FAILED: ${name}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()

  execute_process(
    COMMAND "${armNm}" "${object}"
    RESULT_VARIABLE nmResult
    OUTPUT_VARIABLE symbols
    ERROR_QUIET)
  if(NOT nmResult EQUAL 0)
    message("NM FAILED: ${name}")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()

  string(REGEX MATCHALL "[^\n]*__aeabi_[fd][^\n]*" helpers "${symbols}")

  if(name MATCHES "^control_")
    if(helpers STREQUAL "")
      message("CONTROL BLIND: ${name} references no soft-float helper, the check proves nothing")
      math(EXPR failures "${failures} + 1")
    else()
      message("control ok: ${name} references soft float")
    endif()
  else()
    if(NOT helpers STREQUAL "")
      message("SOFT FLOAT: ${name}")
      foreach(helper IN LISTS helpers)
        message("${helper}")
      endforeach()
      math(EXPR failures "${failures} + 1")
    else()
      message("ok: ${name}")
    endif()
  endif()
endforeach()

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} object check(s) failed")
endif()

message("all IQ object checks passed")
