# Checks that RejectSpringRange.cpp does not compile, and that its diagnostic names the static_assert message "spring needs a number
# type". The snippet is compiled with -fsyntax-only, -std=c++23 and the shim and Common include roots. The inputs are VHAL_CXX (the
# compiler) and VHAL_ROOT (the repository root).
cmake_minimum_required(VERSION 3.25)

set(snippetName "RejectSpringRange.cpp")
set(snippet "${CMAKE_CURRENT_LIST_DIR}/${snippetName}")
set(expected "spring needs a number type")

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -fsyntax-only
    -I "${CMAKE_CURRENT_LIST_DIR}/shim"
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
