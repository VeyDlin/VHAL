# Host checks of the regex matcher: the tests built with the device flags, the syntax table against std::regex, the generated patterns
# in four parts, the builder error snippets, the captures loop that is emitted only with Find, the board fixtures, the frame sizes
# for the Cortex-M4, and the C++ blocks of the wiki pages. The sanitizers run the host tests when the root is configured with
# --sanitize (./test.cmd --sanitize Regex).

vhal_test(
  GLOB Test*.cpp
  INCLUDES "${VHAL_ROOT}/Common"
  OPTIONS -fno-exceptions -fno-rtti)

vhal_test(
  CompareSyntax.cpp
  INCLUDES "${VHAL_ROOT}/Common")

vhal_fail(
  BuilderErrors
  INCLUDES "${VHAL_ROOT}/Common"
  OPTIONS -fno-exceptions -fno-rtti)

foreach(part IN ITEMS 0 1 2 3)
  vhal_check(CheckGenerated.cmake ARGS "${part}")
endforeach()

vhal_check(CheckNotEmitted.cmake)
vhal_check(CheckBoardFixtures.cmake)
vhal_check(CheckFrames.cmake)
vhal_check(CheckWikiSnippets.cmake)
