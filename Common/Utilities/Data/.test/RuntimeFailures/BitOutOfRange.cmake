# Builds BitOutOfRange.cpp and runs it. The program must stop with a failure, and its output must contain the text the host System::Abort
# prints ("sim abort:"). A run that returns normally fails the check. The inputs are VHAL_CXX (the compiler), VHAL_ROOT (the repository
# root, whose .test/shim holds the shared host VHAL.h) and VHAL_WORK_DIR (a folder for the executable).
cmake_minimum_required(VERSION 3.25)

set(source "${CMAKE_CURRENT_LIST_DIR}/BitOutOfRange.cpp")
set(exe "${VHAL_WORK_DIR}/BitOutOfRange")
if(WIN32)
  string(APPEND exe ".exe")
endif()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror
    -I "${VHAL_ROOT}/.test/shim"
    -I "${VHAL_ROOT}/Periphery"
    -I "${VHAL_ROOT}/Common"
    -o "${exe}" "${source}"
  RESULT_VARIABLE buildResult)
if(NOT buildResult EQUAL 0)
  message(FATAL_ERROR "BUILD FAILED: BitOutOfRange")
endif()

execute_process(
  COMMAND "${exe}"
  RESULT_VARIABLE runResult
  OUTPUT_VARIABLE runOut
  ERROR_VARIABLE runErr)

if(runResult EQUAL 0)
  message(FATAL_ERROR "BIT OUT OF RANGE DID NOT STOP THE PROGRAM")
endif()

string(FIND "${runErr}${runOut}" "sim abort:" abortPosition)
if(abortPosition EQUAL -1)
  message(FATAL_ERROR "BIT OUT OF RANGE STOPPED WITHOUT THE ABORT MESSAGE")
endif()

message("BitOutOfRange: stopped by the assertion")
