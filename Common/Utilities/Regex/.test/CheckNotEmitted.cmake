# Checks that the captures loop of the regex matcher is emitted only for a program that calls Find. NotEmitted.cpp is compiled twice to an
# object file: once with Matches only, which must have no symbol of RegexRunCaptures, and once with REGEX_USE_FIND, which must have one.
# The second part proves that nm can see the symbol. The script fails when either part does not hold, and when nm cannot read an object.
# The inputs are VHAL_CXX (the host compiler), VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

get_filename_component(compilerDir "${VHAL_CXX}" DIRECTORY)
find_program(regexNm NAMES nm HINTS "${compilerDir}")
if(regexNm STREQUAL "regexNm-NOTFOUND")
  message(FATAL_ERROR "nm is absent: the captures loop check cannot read the objects")
endif()

# -O0 keeps the loop as its own function. At -O2 GCC inlines the one caller into Find, and the symbol would not show.
set(notEmittedFlags -std=c++23 -O0 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -I "${VHAL_ROOT}/Common")
set(notEmittedSource "${CMAKE_CURRENT_LIST_DIR}/NotEmitted.cpp")

execute_process(
  COMMAND "${VHAL_CXX}" ${notEmittedFlags} -c -o "${VHAL_WORK_DIR}/matches.o" "${notEmittedSource}"
  RESULT_VARIABLE matchesBuild
  ERROR_QUIET
  OUTPUT_QUIET)
if(NOT matchesBuild EQUAL 0)
  message(FATAL_ERROR "BUILD FAILED: NotEmitted.cpp (Matches only)")
endif()

execute_process(
  COMMAND "${VHAL_CXX}" ${notEmittedFlags} -DREGEX_USE_FIND -c -o "${VHAL_WORK_DIR}/find.o" "${notEmittedSource}"
  RESULT_VARIABLE findBuild
  ERROR_QUIET
  OUTPUT_QUIET)
if(NOT findBuild EQUAL 0)
  message(FATAL_ERROR "BUILD FAILED: NotEmitted.cpp (Find)")
endif()

execute_process(
  COMMAND "${regexNm}" "${VHAL_WORK_DIR}/matches.o"
  RESULT_VARIABLE matchesNm
  OUTPUT_VARIABLE matchesSymbols
  ERROR_QUIET)
execute_process(
  COMMAND "${regexNm}" "${VHAL_WORK_DIR}/find.o"
  RESULT_VARIABLE findNm
  OUTPUT_VARIABLE findSymbols
  ERROR_QUIET)
if(NOT matchesNm EQUAL 0 OR NOT findNm EQUAL 0)
  message(FATAL_ERROR "nm could not read the objects")
endif()

string(FIND "${matchesSymbols}" "RegexRunCaptures" matchesPosition)
if(NOT matchesPosition EQUAL -1)
  message(FATAL_ERROR "FAILED: the Matches-only object has a symbol of RegexRunCaptures")
endif()

string(FIND "${findSymbols}" "RegexRunCaptures" findPosition)
if(findPosition EQUAL -1)
  message(FATAL_ERROR "FAILED: the Find object has no symbol of RegexRunCaptures, so the check cannot see it")
endif()

message("captures loop is emitted only with Find")
