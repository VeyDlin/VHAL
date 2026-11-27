# Compiles the scale set and the dialect set of the AT description with the host compiler, with -Wall -Wextra -Werror. The script
# fails when either set does not compile. The inputs are VHAL_CXX (the host compiler), VHAL_ROOT and VHAL_WORK_DIR (a folder for the
# objects).
cmake_minimum_required(VERSION 3.25)

include("${CMAKE_CURRENT_LIST_DIR}/AtCompile.cmake")

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(atHostFlags -std=c++23 -Wall -Wextra -Werror)
set(atFailures 0)

atCompileObject(scaleResult "${VHAL_CXX}" "${atTestDir}/ScaleSet.cpp" "${VHAL_WORK_DIR}/ScaleSet.o"
  ${atHostFlags} ${atIncludeFlags})
if(scaleResult STREQUAL "ok")
  message("scale set compiles on the host")
else()
  message("SCALE SET FAILED: host")
  math(EXPR atFailures "${atFailures} + 1")
endif()

atCompileObject(dialectResult "${VHAL_CXX}" "${atTestDir}/DialectAll.cpp" "${VHAL_WORK_DIR}/DialectAll.o"
  ${atHostFlags} ${atIncludeFlags})
if(NOT dialectResult STREQUAL "ok")
  message("DIALECT SET FAILED: host (with every dialect item)")
  math(EXPR atFailures "${atFailures} + 1")
endif()

if(atFailures GREATER 0)
  message(FATAL_ERROR "${atFailures} host check(s) failed")
endif()

message("all AT host checks passed")
