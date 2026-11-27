# Host checks of the regex matcher: the tests built with the device flags, the same tests with the address and undefined-behaviour
# sanitizers (skipped when the toolchain cannot run them), the syntax table against std::regex, the generated patterns in four parts,
# the builder error snippets, the captures loop that is emitted only with Find, the board fixtures, the frame sizes for the Cortex-M4,
# and the C++ blocks of the wiki pages.

vhal_test(
  GLOB Test*.cpp
  INCLUDES "${VHAL_ROOT}/Common"
  OPTIONS -fno-exceptions -fno-rtti)

vhal_test(
  CompareSyntax.cpp
  INCLUDES "${VHAL_ROOT}/Common")

file(GLOB regexTests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp")
list(SORT regexTests)
foreach(regexTest IN LISTS regexTests)
  get_filename_component(testStem "${regexTest}" NAME_WE)
  vhal_check(CheckSanitized.cmake ARGS "${testStem}")
endforeach()

file(GLOB snippets RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/BuilderErrors" "${CMAKE_CURRENT_SOURCE_DIR}/BuilderErrors/*.cpp")
list(SORT snippets)
foreach(snippet IN LISTS snippets)
  get_filename_component(snippetStem "${snippet}" NAME_WE)
  vhal_check(CheckSnippet.cmake ARGS "${snippetStem}")
endforeach()

foreach(part IN ITEMS 0 1 2 3)
  vhal_check(CheckGenerated.cmake ARGS "${part}")
endforeach()

vhal_check(CheckNotEmitted.cmake)
vhal_check(CheckBoardFixtures.cmake)
vhal_check(CheckFrames.cmake)
vhal_check(CheckWikiSnippets.cmake)
