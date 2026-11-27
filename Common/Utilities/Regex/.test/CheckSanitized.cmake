# Builds one test of the regex matcher again with the address and undefined-behaviour sanitizers, and runs it. The device flags are kept,
# with -g and -fsanitize=address,undefined -fno-sanitize-recover=undefined. The sanitizer runtimes are probed first with a trivial
# program: a toolchain that cannot link and run them gets the VHAL_SKIP marker, and the test is skipped. The last argument is the stem
# of the test source (TestLinear). The inputs are VHAL_CXX (the compiler), VHAL_ROOT and VHAL_WORK_DIR (a folder for the executables).
cmake_minimum_required(VERSION 3.25)

math(EXPR lastArgIndex "${CMAKE_ARGC} - 1")
set(testStem "${CMAKE_ARGV${lastArgIndex}}")
set(testSource "${CMAKE_CURRENT_LIST_DIR}/${testStem}.cpp")

if(WIN32)
  set(exeSuffix ".exe")
else()
  set(exeSuffix "")
endif()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

file(WRITE "${VHAL_WORK_DIR}/SanitizerProbe.cpp" "int main() { return 0; }\n")
execute_process(
  COMMAND "${VHAL_CXX}" -fsanitize=address,undefined
    -o "${VHAL_WORK_DIR}/SanitizerProbe${exeSuffix}" "${VHAL_WORK_DIR}/SanitizerProbe.cpp"
  RESULT_VARIABLE probeBuildResult
  OUTPUT_QUIET
  ERROR_QUIET)
set(probeRunResult 1)
if(probeBuildResult EQUAL 0)
  execute_process(
    COMMAND "${VHAL_WORK_DIR}/SanitizerProbe${exeSuffix}"
    RESULT_VARIABLE probeRunResult
    OUTPUT_QUIET
    ERROR_QUIET)
endif()

if(NOT probeBuildResult EQUAL 0 OR NOT probeRunResult EQUAL 0)
  message("VHAL_SKIP: sanitizers unavailable (the toolchain cannot link and run -fsanitize=address,undefined)")
  return()
endif()

set(sanitized "${VHAL_WORK_DIR}/${testStem}-sanitized${exeSuffix}")
execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -fno-exceptions -fno-rtti
    -g -fsanitize=address,undefined -fno-sanitize-recover=undefined
    -I "${VHAL_ROOT}/Common"
    -o "${sanitized}" "${testSource}"
  RESULT_VARIABLE buildResult
  ERROR_VARIABLE buildErr
  OUTPUT_QUIET)
if(NOT buildResult EQUAL 0)
  message("${buildErr}")
  message(FATAL_ERROR "BUILD FAILED: ${testStem}-sanitized")
endif()

execute_process(
  COMMAND "${sanitized}"
  RESULT_VARIABLE runResult)
if(NOT runResult EQUAL 0)
  message(FATAL_ERROR "TEST FAILED: ${testStem}-sanitized")
endif()
