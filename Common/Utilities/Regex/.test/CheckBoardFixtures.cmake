# Runs WriteBoardFixtures.cpp again and checks that its output is the committed fixtures/Board.inc, so the file is what its program writes.
# The program's output goes to the work folder. The script fails on a difference. The inputs are VHAL_CXX (the host compiler) and
# VHAL_WORK_DIR (a folder for the executable and the output).
cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(fixturesDir "${CMAKE_CURRENT_LIST_DIR}/fixtures")
if(WIN32)
  set(exeSuffix ".exe")
else()
  set(exeSuffix "")
endif()
set(writer "${VHAL_WORK_DIR}/WriteBoardFixtures${exeSuffix}")

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -o "${writer}" "${fixturesDir}/WriteBoardFixtures.cpp"
  RESULT_VARIABLE buildResult
  ERROR_VARIABLE buildErr
  OUTPUT_QUIET)
if(NOT buildResult EQUAL 0)
  message("${buildErr}")
  message(FATAL_ERROR "BUILD FAILED: WriteBoardFixtures")
endif()

execute_process(
  COMMAND "${writer}"
  RESULT_VARIABLE runResult
  OUTPUT_FILE "${VHAL_WORK_DIR}/Board.inc")
if(NOT runResult EQUAL 0)
  message(FATAL_ERROR "RUN FAILED: WriteBoardFixtures")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${fixturesDir}/Board.inc" "${VHAL_WORK_DIR}/Board.inc"
  RESULT_VARIABLE compareResult)
if(NOT compareResult EQUAL 0)
  message(FATAL_ERROR "BOARD FIXTURES DIFFER: fixtures/Board.inc is not the output of WriteBoardFixtures.cpp")
endif()

message("board fixtures match WriteBoardFixtures.cpp")
