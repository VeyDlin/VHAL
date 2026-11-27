# Checks that each header of the register bus folder compiles alone. Each header is included by its installed path in a one-line
# translation unit, which must pass -fsyntax-only with -std=c++23 -Wall -Wextra -Werror -Wconversion. The include roots are Common
# and this folder. The inputs are VHAL_CXX (the compiler), VHAL_ROOT (the repository root) and VHAL_WORK_DIR (a folder it may empty).
cmake_minimum_required(VERSION 3.25)

set(busDir "${VHAL_ROOT}/Common/Drivers/Interface/RegisterBus")

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

file(GLOB headers "${busDir}/*.h")
list(SORT headers)

set(headerFailures 0)
foreach(header IN LISTS headers)
  get_filename_component(headerName "${header}" NAME)
  set(probe "${VHAL_WORK_DIR}/header_${headerName}.cpp")
  file(WRITE "${probe}" "#include <Drivers/Interface/RegisterBus/${headerName}>\n")

  execute_process(
    COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -Wconversion -fsyntax-only
      -I "${VHAL_ROOT}/Common"
      -I "${CMAKE_CURRENT_LIST_DIR}"
      "${probe}"
    RESULT_VARIABLE compileResult
    OUTPUT_VARIABLE compileOut
    ERROR_VARIABLE compileErr)

  if(NOT compileResult EQUAL 0)
    message("${compileOut}${compileErr}")
    message("HEADER DOES NOT COMPILE ALONE: ${headerName}")
    math(EXPR headerFailures "${headerFailures} + 1")
  endif()
endforeach()

if(headerFailures GREATER 0)
  message(FATAL_ERROR "${headerFailures} register bus header(s) do not compile alone")
endif()

message("all register bus headers compile alone")
