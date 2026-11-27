# Checks that Cobs.h compiles alone: a translation unit that has nothing but the include of the header must pass -fsyntax-only with
# -std=c++23 -Wall -Wextra -Werror. The script writes the one-line probe into its work folder. It fails when the header does not
# compile. Inputs: VHAL_CXX (the host compiler), VHAL_ROOT (the repository root) and VHAL_WORK_DIR (a folder it may empty).
cmake_minimum_required(VERSION 3.25)

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(probe "${VHAL_WORK_DIR}/HeaderAlone.cpp")
file(WRITE "${probe}" "#include <Utilities/Encoding/COBS/Cobs.h>\n")

execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -fsyntax-only -I "${VHAL_ROOT}/Common" "${probe}"
  RESULT_VARIABLE headerResult
  OUTPUT_VARIABLE headerOut
  ERROR_VARIABLE headerErr)

if(NOT headerResult EQUAL 0)
  message("${headerOut}${headerErr}")
  message(FATAL_ERROR "HEADER FAILED: Cobs.h does not compile alone")
endif()

message("Cobs.h compiles alone")
