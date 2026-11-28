# The -Os check: a descriptor that nothing uses leaves no symbol in the object file. UnusedDescriptor.cpp is compiled at -Os and nm
# prints its symbols; the name UnusedProbeDescriptor must not appear in the output. The script fails when the probe does not build,
# when nm cannot read the object, or when the name appears. The inputs are VHAL_CXX (the host compiler), VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

get_filename_component(compilerDir "${VHAL_CXX}" DIRECTORY)
find_program(vhalNm NAMES nm HINTS "${compilerDir}")
if(vhalNm STREQUAL "vhalNm-NOTFOUND")
  message(FATAL_ERROR "nm is absent: the unused descriptor check cannot read the object")
endif()

set(unusedObject "${VHAL_WORK_DIR}/UnusedDescriptor.o")

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Os -Wall -Wextra -Werror
    -I "${VHAL_ROOT}/.test/shim"
    -I "${VHAL_ROOT}/Periphery"
    -I "${VHAL_ROOT}/Common"
    -I "${CMAKE_CURRENT_LIST_DIR}"
    -c -o "${unusedObject}" "${CMAKE_CURRENT_LIST_DIR}/UnusedDescriptor.cpp"
  RESULT_VARIABLE buildResult
  ERROR_VARIABLE buildErr
  OUTPUT_QUIET)

if(NOT buildResult EQUAL 0)
  message("${buildErr}")
  message(FATAL_ERROR "UNUSED DESCRIPTOR PROBE DOES NOT BUILD")
endif()

execute_process(
  COMMAND "${vhalNm}" -C "${unusedObject}"
  RESULT_VARIABLE nmResult
  OUTPUT_VARIABLE nmOut
  ERROR_QUIET)

if(NOT nmResult EQUAL 0)
  message(FATAL_ERROR "nm could not read ${unusedObject}")
endif()

string(FIND "${nmOut}" "UnusedProbeDescriptor" leftPosition)
if(NOT leftPosition EQUAL -1)
  message(FATAL_ERROR "UNUSED DESCRIPTOR LEFT A SYMBOL AT -Os")
endif()

message("an unused descriptor leaves no symbol at -Os")
