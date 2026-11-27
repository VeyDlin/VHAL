# Checks one compile-fail snippet, the test that vhal_fail adds for each *.cpp file of a folder. The snippet must fail to compile
# with -fsyntax-only, and its diagnostic must contain the text of its first line, "// expect: <text>". A second line
# "// one error" asks for exactly one error line in the diagnostic. The inputs are VHAL_CXX (the compiler), VHAL_SNIPPET (the file)
# and VHAL_INCLUDES (the include directories, a list). The compiler runs with LC_ALL=C, and CR line ends are ignored.
cmake_minimum_required(VERSION 3.25)

set(ENV{LC_ALL} C)

get_filename_component(snippetName "${VHAL_SNIPPET}" NAME)

file(READ "${VHAL_SNIPPET}" snippetText)
string(REPLACE "\r" "" snippetText "${snippetText}")
string(REGEX MATCH "^[^\n]*" firstLine "${snippetText}")
string(REGEX REPLACE "^[^\n]*\n?" "" restText "${snippetText}")
string(REGEX MATCH "^[^\n]*" secondLine "${restText}")

if(NOT firstLine MATCHES "^// expect: ")
  message(FATAL_ERROR "NO EXPECT HEADER: ${snippetName} (the first line must be \"// expect: <text>\")")
endif()

string(REGEX REPLACE "^// expect: " "" expected "${firstLine}")
if(expected STREQUAL "")
  message(FATAL_ERROR "EMPTY EXPECT TEXT: ${snippetName}")
endif()

set(includeFlags "")
foreach(includeDir IN LISTS VHAL_INCLUDES)
  list(APPEND includeFlags "-I" "${includeDir}")
endforeach()

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -fsyntax-only ${includeFlags} "${VHAL_SNIPPET}"
  RESULT_VARIABLE compileResult
  OUTPUT_VARIABLE compileOut
  ERROR_VARIABLE compileErr)

if(compileResult EQUAL 0)
  message(FATAL_ERROR "COMPILED, an error was expected: ${snippetName}")
endif()

set(diagnostic "${compileOut}${compileErr}")

string(FIND "${diagnostic}" "${expected}" expectedPosition)
if(expectedPosition EQUAL -1)
  message(FATAL_ERROR "WRONG DIAGNOSTIC: ${snippetName} (expected text: ${expected})")
endif()

if(secondLine STREQUAL "// one error")
  string(REGEX MATCHALL ": error:" errorLines "${diagnostic}")
  list(LENGTH errorLines errorCount)
  if(NOT errorCount EQUAL 1)
    message(FATAL_ERROR "NOT ONE ERROR: ${snippetName} (${errorCount} error lines)")
  endif()
endif()

message("rejected as expected: ${snippetName}")
