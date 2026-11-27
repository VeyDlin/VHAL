# Checks that every header of the register map compiles alone: a probe file that includes only that header is compiled with -fsyntax-only,
# with -std=c++23 -Wall -Wextra -Werror -Wconversion. The script fails when a header does not compile. The inputs are VHAL_CXX (the
# host compiler), VHAL_ROOT and VHAL_WORK_DIR (a folder for the probe files).
cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(regMapDir "${VHAL_ROOT}/Common/Utilities/Data/RegisterMap")
set(regMapFlags
  -std=c++23 -Wall -Wextra -Werror -Wconversion
  -I "${VHAL_ROOT}/Common"
  -I "${CMAKE_CURRENT_LIST_DIR}")

file(GLOB headers "${regMapDir}/*.h")
list(SORT headers)

set(failures 0)

foreach(header IN LISTS headers)
  get_filename_component(headerName "${header}" NAME)
  set(probe "${VHAL_WORK_DIR}/header_${headerName}.cpp")
  file(WRITE "${probe}" "#include <Utilities/Data/RegisterMap/${headerName}>\n")

  execute_process(
    COMMAND "${VHAL_CXX}" ${regMapFlags} -fsyntax-only "${probe}"
    RESULT_VARIABLE probeResult
    ERROR_VARIABLE probeErr
    OUTPUT_QUIET)

  if(NOT probeResult EQUAL 0)
    message("${probeErr}")
    message("HEADER DOES NOT COMPILE ALONE: ${headerName}")
    math(EXPR failures "${failures} + 1")
  endif()
endforeach()

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} RegisterMap header(s) do not compile alone")
endif()

message("every RegisterMap header compiles alone")
