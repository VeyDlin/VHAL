# Compares the generated patterns of one part with std::regex. GenerateCases.cpp writes the part of the case list that the part number names
# (the list is split in four parts), and CompareGenerated.cpp is built with that list as REGEX_CASES_FILE and run. The part number is the
# last argument. The four parts are four tests, so ctest runs them in parallel. Each part has its own work folder. The inputs are
# VHAL_CXX (the compiler), VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

set(generatedParts 4)

math(EXPR lastArgIndex "${CMAKE_ARGC} - 1")
set(part "${CMAKE_ARGV${lastArgIndex}}")

if(WIN32)
  set(exeSuffix ".exe")
else()
  set(exeSuffix "")
endif()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(hostFlags -std=c++23 -Wall -Wextra -Werror)
set(generator "${VHAL_WORK_DIR}/GenerateCases${exeSuffix}")
execute_process(
  COMMAND "${VHAL_CXX}" ${hostFlags} -o "${generator}" "${CMAKE_CURRENT_LIST_DIR}/GenerateCases.cpp"
  RESULT_VARIABLE buildResult
  ERROR_VARIABLE buildErr
  OUTPUT_QUIET)
if(NOT buildResult EQUAL 0)
  message("${buildErr}")
  message(FATAL_ERROR "BUILD FAILED: GenerateCases")
endif()

set(casesFile "GeneratedCases${part}.inc")
execute_process(
  COMMAND "${generator}" "${part}" "${generatedParts}"
  RESULT_VARIABLE generateResult
  OUTPUT_FILE "${VHAL_WORK_DIR}/${casesFile}")
if(NOT generateResult EQUAL 0)
  message(FATAL_ERROR "GenerateCases failed for part ${part}")
endif()

set(compareExe "${VHAL_WORK_DIR}/CompareGenerated${part}${exeSuffix}")
execute_process(
  COMMAND "${VHAL_CXX}" ${hostFlags}
    -I "${VHAL_ROOT}/Common"
    "-DREGEX_CASES_FILE=\"${casesFile}\""
    -I "${VHAL_WORK_DIR}"
    -o "${compareExe}" "${CMAKE_CURRENT_LIST_DIR}/CompareGenerated.cpp"
  RESULT_VARIABLE compileResult
  ERROR_VARIABLE compileErr
  OUTPUT_QUIET)
if(NOT compileResult EQUAL 0)
  message("${compileErr}")
  message(FATAL_ERROR "BUILD FAILED: CompareGenerated, part ${part}")
endif()

execute_process(
  COMMAND "${compareExe}"
  RESULT_VARIABLE runResult)
if(NOT runResult EQUAL 0)
  message(FATAL_ERROR "CHECK FAILED: generated patterns, part ${part}")
endif()
