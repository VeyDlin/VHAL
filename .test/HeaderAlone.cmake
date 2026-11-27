# Checks that one header compiles alone: a translation unit whose only line is the include of the header must pass -fsyntax-only with
# -std=c++23 -Wall -Wextra -Werror and the extra flags of the test. vhal_headers declares one test for each header. The inputs are
# VHAL_CXX (the compiler), VHAL_PROBE_LINE (the include line of the header), VHAL_INCLUDES (the include directories, a list),
# VHAL_OPTIONS (the extra flags, a list) and VHAL_WORK_DIR (a folder for the probe, which the script empties).
cmake_minimum_required(VERSION 3.25)

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(probe "${VHAL_WORK_DIR}/HeaderProbe.cpp")
file(WRITE "${probe}" "${VHAL_PROBE_LINE}\n")

set(includeFlags "")
foreach(includeDir IN LISTS VHAL_INCLUDES)
  list(APPEND includeFlags "-I" "${includeDir}")
endforeach()

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -fsyntax-only ${VHAL_OPTIONS} ${includeFlags} "${probe}"
  RESULT_VARIABLE compileResult
  OUTPUT_VARIABLE compileOut
  ERROR_VARIABLE compileErr)

if(NOT compileResult EQUAL 0)
  message("${compileOut}${compileErr}")
  message(FATAL_ERROR "HEADER DOES NOT COMPILE ALONE: ${VHAL_PROBE_LINE}")
endif()

message("header compiles alone: ${VHAL_PROBE_LINE}")
