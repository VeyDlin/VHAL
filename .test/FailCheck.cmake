# Checks one compile-fail snippet, the test that vhal_fail adds for each *.cpp file of a folder. The snippet must fail to compile with
# -fsyntax-only, and its diagnostic must contain the text of its first line, "// expect: <text>". The second line is optional:
# "// one error" asks for exactly one error line in the diagnostic, and "// expect-value: <text>" asks for the value <text> to be
# printed with it, as "Value = <text>]" by GCC or as ", <text>>" by Clang. The inputs are VHAL_CXX (the compiler), VHAL_SNIPPET
# (the file), VHAL_INCLUDES (the include directories, a list) and VHAL_OPTIONS (the extra flags, a list).
#
# The compiler runs with LC_ALL=C. The expected texts are the English messages of the compiler, and a compiler that runs in another
# locale prints translated messages that do not contain them. LC_ALL is set in the environment of this script, so the compiler
# started here inherits it. The diagnostic is read with GCC's escapes undone (a backslash and a quote inside a template argument),
# and CR line ends are ignored in the snippet.
cmake_minimum_required(VERSION 3.25)

set(ENV{LC_ALL} C)

get_filename_component(snippetName "${VHAL_SNIPPET}" NAME)

file(READ "${VHAL_SNIPPET}" snippetText)
string(REPLACE "\r" "" snippetText "${snippetText}")

string(FIND "${snippetText}" "\n" firstEnd)
if(firstEnd EQUAL -1)
  set(firstLine "${snippetText}")
  set(restText "")
else()
  string(SUBSTRING "${snippetText}" 0 ${firstEnd} firstLine)
  math(EXPR restStart "${firstEnd} + 1")
  string(SUBSTRING "${snippetText}" ${restStart} -1 restText)
endif()

string(FIND "${restText}" "\n" secondEnd)
if(secondEnd EQUAL -1)
  set(secondLine "${restText}")
else()
  string(SUBSTRING "${restText}" 0 ${secondEnd} secondLine)
endif()

if(NOT firstLine MATCHES "^// expect: ")
  message(FATAL_ERROR "NO EXPECT HEADER: ${snippetName} (the first line must be \"// expect: <text>\")")
endif()

string(REGEX REPLACE "^// expect: " "" expected "${firstLine}")
if(expected STREQUAL "")
  message(FATAL_ERROR "EMPTY EXPECT TEXT: ${snippetName}")
endif()

set(expectOneError OFF)
if(secondLine STREQUAL "// one error")
  set(expectOneError ON)
endif()

set(expectedValue "")
if(secondLine MATCHES "^// expect-value: ")
  string(REGEX REPLACE "^// expect-value: " "" expectedValue "${secondLine}")
  if(expectedValue STREQUAL "")
    message(FATAL_ERROR "EMPTY EXPECT-VALUE TEXT: ${snippetName}")
  endif()
endif()

set(includeFlags "")
foreach(includeDir IN LISTS VHAL_INCLUDES)
  list(APPEND includeFlags "-I" "${includeDir}")
endforeach()

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -fsyntax-only ${VHAL_OPTIONS} ${includeFlags} "${VHAL_SNIPPET}"
  RESULT_VARIABLE compileResult
  OUTPUT_VARIABLE compileOut
  ERROR_VARIABLE compileErr)

if(compileResult EQUAL 0)
  message(FATAL_ERROR "COMPILED, an error was expected: ${snippetName}")
endif()

# GCC escapes the quotes and the backslashes of a template string argument (\' and \\). The text is read back unescaped.
set(diagnostic "${compileOut}${compileErr}")
string(REPLACE "\\\\" "\\" diagnostic "${diagnostic}")
string(REPLACE "\\'" "'" diagnostic "${diagnostic}")

string(FIND "${diagnostic}" "${expected}" expectedPosition)
if(expectedPosition EQUAL -1)
  message(FATAL_ERROR "WRONG DIAGNOSTIC: ${snippetName} (expected text: ${expected})")
endif()

if(expectOneError)
  string(REGEX MATCHALL "[^\n]*: error:[^\n]*" errorLines "${diagnostic}")
  list(LENGTH errorLines errorCount)
  if(NOT errorCount EQUAL 1)
    message(FATAL_ERROR "NOT ONE ERROR: ${snippetName} (${errorCount} error lines)")
  endif()
endif()

if(NOT expectedValue STREQUAL "")
  string(FIND "${diagnostic}" "Value = ${expectedValue}]" gccPosition)
  string(FIND "${diagnostic}" ", ${expectedValue}>" clangPosition)
  if(gccPosition EQUAL -1 AND clangPosition EQUAL -1)
    message(FATAL_ERROR "WRONG VALUE: ${snippetName} (expected value: ${expectedValue})")
  endif()
endif()

message("rejected as expected: ${snippetName}")
