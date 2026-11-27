# Compiles every ```cpp block of the regex wiki pages (.wiki/Common/Utilities/Regex/*.md) as its own translation unit, with the device
# flags and syntax only. A block must be complete on its own: its includes, its names and its static_asserts. Blocks that are not C++ use
# a ```text fence and are not compiled. The script fails when a block does not compile, or when a page has no C++ block. The inputs are
# VHAL_CXX (the host compiler), VHAL_ROOT and VHAL_WORK_DIR (a folder for the blocks).
cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(wikiDir "${VHAL_ROOT}/.wiki/Common/Utilities/Regex")
file(GLOB pages "${wikiDir}/*.md")
list(SORT pages)

set(snippetCount 0)
set(failures 0)

foreach(page IN LISTS pages)
  get_filename_component(pageName "${page}" NAME_WE)

  # The lines are cut from the text one at a time: a page's text is not split into a list, because the list would not keep its
  # lines apart when they hold quotes and brackets.
  file(READ "${page}" pageText)

  set(inBlock OFF)
  set(blockCount 0)
  set(blockText "")
  while(NOT pageText STREQUAL "")
    string(FIND "${pageText}" "\n" lineEnd)
    if(lineEnd EQUAL -1)
      set(pageLine "${pageText}")
      set(pageText "")
    else()
      string(SUBSTRING "${pageText}" 0 ${lineEnd} pageLine)
      math(EXPR restStart "${lineEnd} + 1")
      string(SUBSTRING "${pageText}" ${restStart} -1 pageText)
    endif()

    if(pageLine STREQUAL "```cpp")
      set(inBlock ON)
      math(EXPR blockCount "${blockCount} + 1")
      set(blockText "")
    elseif(pageLine STREQUAL "```")
      if(inBlock)
        file(WRITE "${VHAL_WORK_DIR}/${pageName}-${blockCount}.cpp" "${blockText}")
      endif()
      set(inBlock OFF)
    elseif(inBlock)
      set(blockText "${blockText}${pageLine}\n")
    endif()
  endwhile()

  if(blockCount EQUAL 0)
    message("NO C++ BLOCK: ${pageName}.md")
    math(EXPR failures "${failures} + 1")
    continue()
  endif()

  foreach(index RANGE 1 ${blockCount})
    math(EXPR snippetCount "${snippetCount} + 1")
    set(snippet "${VHAL_WORK_DIR}/${pageName}-${index}.cpp")

    execute_process(
      COMMAND "${VHAL_CXX}" -std=c++23 -fsyntax-only -Wall -Wextra -Werror -fno-exceptions -fno-rtti
        -I "${VHAL_ROOT}/Common"
        "${snippet}"
      RESULT_VARIABLE compileResult
      ERROR_VARIABLE compileErr
      OUTPUT_QUIET)

    if(NOT compileResult EQUAL 0)
      message("${compileErr}")
      message("SNIPPET FAILED: ${pageName}.md, C++ block ${index}")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} wiki snippet check(s) failed")
endif()

message("all ${snippetCount} regex wiki snippets compile")
