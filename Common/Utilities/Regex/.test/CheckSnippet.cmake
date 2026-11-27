# Checks one builder error snippet of the regex parser: it must fail to compile with -fsyntax-only, -fno-exceptions and -fno-rtti, and its
# diagnostic must contain the text of its first line, "// expect: <text>". A second line, "// expect-value: <number>", names the value of
# the error: the diagnostic must print it as "Value = N]" (GCC) or ", N>" (Clang). The last argument is the stem of the snippet. The inputs
# are VHAL_CXX (the compiler), VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

math(EXPR lastArgIndex "${CMAKE_ARGC} - 1")
set(snippetStem "${CMAKE_ARGV${lastArgIndex}}")
set(snippetName "${snippetStem}.cpp")
set(snippet "${CMAKE_CURRENT_LIST_DIR}/BuilderErrors/${snippetName}")

file(READ "${snippet}" snippetText)

# The first and the second line, without their line ends. A checkout with core.autocrlf gives the snippets CRLF line ends; the CR is
# not part of the expected text.
string(FIND "${snippetText}" "\n" firstEnd)
if(firstEnd EQUAL -1)
  set(firstLine "${snippetText}")
  set(restText "")
else()
  string(SUBSTRING "${snippetText}" 0 ${firstEnd} firstLine)
  math(EXPR restStart "${firstEnd} + 1")
  string(SUBSTRING "${snippetText}" ${restStart} -1 restText)
endif()
string(REGEX REPLACE "\r$" "" firstLine "${firstLine}")

string(FIND "${restText}" "\n" secondEnd)
if(secondEnd EQUAL -1)
  set(secondLine "${restText}")
else()
  string(SUBSTRING "${restText}" 0 ${secondEnd} secondLine)
endif()
string(REGEX REPLACE "\r$" "" secondLine "${secondLine}")

if(NOT firstLine MATCHES "^// expect: ")
  message(FATAL_ERROR "NO EXPECT HEADER: ${snippetName} (the first line must be \"// expect: <text>\")")
endif()

string(REGEX REPLACE "^// expect: " "" expected "${firstLine}")
if(expected STREQUAL "")
  message(FATAL_ERROR "EMPTY EXPECT TEXT: ${snippetName}")
endif()

set(expectedValue "")
if(secondLine MATCHES "^// expect-value: ")
  string(REGEX REPLACE "^// expect-value: " "" expectedValue "${secondLine}")
endif()

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -fsyntax-only -fno-exceptions -fno-rtti
    -I "${VHAL_ROOT}/Common"
    "${snippet}"
  RESULT_VARIABLE compileResult
  OUTPUT_VARIABLE compileOut
  ERROR_VARIABLE compileErr)

if(compileResult EQUAL 0)
  message(FATAL_ERROR "COMPILED, an error was expected: ${snippetName}")
endif()

# GCC escapes the quotes and the backslashes of a template string argument (\' and \\). The text is read back unescaped.
set(output "${compileOut}${compileErr}")
string(REPLACE "\\\\" "\\" output "${output}")
string(REPLACE "\\'" "'" output "${output}")

string(FIND "${output}" "${expected}" expectedPosition)
if(expectedPosition EQUAL -1)
  message(FATAL_ERROR "WRONG DIAGNOSTIC: ${snippetName} (expected text: ${expected})")
endif()

# GCC prints the value as "Value = N]" in a function signature, and Clang as ", N>" in the template arguments.
if(NOT expectedValue STREQUAL "")
  string(FIND "${output}" "Value = ${expectedValue}]" gccPosition)
  string(FIND "${output}" ", ${expectedValue}>" clangPosition)
  if(gccPosition EQUAL -1 AND clangPosition EQUAL -1)
    message(FATAL_ERROR "WRONG VALUE: ${snippetName} (expected value: ${expectedValue})")
  endif()
endif()

message("rejected as expected: ${snippetName}")
