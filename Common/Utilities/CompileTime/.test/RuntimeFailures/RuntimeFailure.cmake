# Builds RequireRuntimeFailure.cpp for one mode and runs it. The program must stop with a failure, and its output must contain the
# message of the mode: "CompileTime: run-time failure" for plain, "CompileTime: value failure (value 7)" for value. The mode is the
# last argument of the command line. The inputs are VHAL_CXX (the compiler), VHAL_ROOT (the repository root) and VHAL_WORK_DIR
# (a folder for the executable).
cmake_minimum_required(VERSION 3.25)

math(EXPR lastArgIndex "${CMAKE_ARGC} - 1")
set(mode "${CMAKE_ARGV${lastArgIndex}}")
if(mode STREQUAL "plain")
  set(expected "CompileTime: run-time failure")
elseif(mode STREQUAL "value")
  set(expected "CompileTime: value failure (value 7)")
else()
  message(FATAL_ERROR "The mode \"${mode}\" is not plain or value")
endif()

get_filename_component(testDir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(source "${CMAKE_CURRENT_LIST_DIR}/RequireRuntimeFailure.cpp")
set(exe "${VHAL_WORK_DIR}/RuntimeFailure_${mode}")
if(WIN32)
  string(APPEND exe ".exe")
endif()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror
    -I "${VHAL_ROOT}/Common"
    -I "${testDir}"
    -o "${exe}" "${source}"
  RESULT_VARIABLE buildResult)
if(NOT buildResult EQUAL 0)
  message(FATAL_ERROR "BUILD FAILED: RuntimeFailure_${mode}")
endif()

execute_process(
  COMMAND "${exe}" "${mode}"
  RESULT_VARIABLE runResult
  OUTPUT_VARIABLE runOut
  ERROR_VARIABLE runErr)

if(runResult EQUAL 0)
  message(FATAL_ERROR "RUN-TIME FAILURE DID NOT STOP THE PROGRAM: RuntimeFailure_${mode}")
endif()

set(runOutput "${runOut}${runErr}")
string(FIND "${runOutput}" "${expected}" expectedPosition)
if(expectedPosition EQUAL -1)
  message(FATAL_ERROR "RUN-TIME FAILURE MESSAGE MISSING: RuntimeFailure_${mode} (expected text: ${expected})")
endif()

message("RuntimeFailure_${mode}: stopped with its message")
