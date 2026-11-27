# Checks that every header of the VUB core compiles alone: each one is the first include of an otherwise empty translation unit. The
# headers are the ones in the VUB folder and in its block folders (TestKit, Link, Framing, Transform, Engine, Layer, PC, Requests).
# The compiler is VHAL_CXX. The include roots are Common, the adapter shim and Periphery, the same as the tests. The script fails when
# a header does not compile; the diagnostic of each failing header is printed.
cmake_minimum_required(VERSION 3.25)

set(vubHeaderDir "${CMAKE_CURRENT_LIST_DIR}/..")

set(vubHeaders "")
file(GLOB vubRootHeaders "${vubHeaderDir}/*.h")
list(SORT vubRootHeaders)
list(APPEND vubHeaders ${vubRootHeaders})

foreach(vubFolder IN ITEMS TestKit Link Framing Transform Engine Layer PC Requests)
  file(GLOB vubFolderHeaders "${vubHeaderDir}/${vubFolder}/*.h")
  list(SORT vubFolderHeaders)
  list(APPEND vubHeaders ${vubFolderHeaders})
endforeach()

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")
set(vubEmptySource "${VHAL_WORK_DIR}/Empty.cpp")
file(WRITE "${vubEmptySource}" "")

set(vubIncludeFlags
  -I "${VHAL_ROOT}/Common"
  -I "${VHAL_ROOT}/Periphery/Adapter/.test/shim"
  -I "${VHAL_ROOT}/Periphery")

set(vubHeaderFailures 0)
foreach(vubHeader IN LISTS vubHeaders)
  execute_process(
    COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -fsyntax-only ${vubIncludeFlags}
      -include "${vubHeader}" -x c++ "${vubEmptySource}"
    RESULT_VARIABLE vubHeaderResult
    OUTPUT_QUIET
    ERROR_VARIABLE vubHeaderErr)

  if(NOT vubHeaderResult EQUAL 0)
    file(RELATIVE_PATH vubRelativeHeader "${VHAL_ROOT}" "${vubHeader}")
    message("${vubHeaderErr}")
    message("HEADER DOES NOT COMPILE ALONE: ${vubRelativeHeader}")
    math(EXPR vubHeaderFailures "${vubHeaderFailures} + 1")
  endif()
endforeach()

if(vubHeaderFailures GREATER 0)
  message(FATAL_ERROR "${vubHeaderFailures} header(s) of the VUB core do not compile alone")
endif()

message("all headers of the VUB core compile alone")
