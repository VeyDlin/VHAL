# Checks that RejectAngleRange.cpp does not compile, and that its diagnostic names the RangeAtLeast message. The snippet is compiled
# with -fsyntax-only, -std=c++23 and the Common include root. The inputs are VHAL_CXX (the compiler) and VHAL_ROOT (the repository
# root).
cmake_minimum_required(VERSION 3.25)

set(snippetName "RejectAngleRange.cpp")
set(snippet "${CMAKE_CURRENT_LIST_DIR}/${snippetName}")
set(expected "RangeAtLeast")

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -fsyntax-only
    -I "${VHAL_ROOT}/Common"
    "${snippet}"
  RESULT_VARIABLE compileResult
  OUTPUT_VARIABLE compileOut
  ERROR_VARIABLE compileErr)

if(compileResult EQUAL 0)
  message(FATAL_ERROR "REJECT COMPILED: ${snippetName}")
endif()

string(FIND "${compileOut}${compileErr}" "${expected}" expectedPosition)
if(expectedPosition EQUAL -1)
  message(FATAL_ERROR "REJECT FAILED FOR ANOTHER REASON: ${snippetName}")
endif()

message("rejected as expected: ${snippetName}")
