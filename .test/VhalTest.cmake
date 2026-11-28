# The functions a module's .test/CMakeLists.txt calls to declare its checks. Each test is named <module label>.<name> for a host test
# and <module label>.<kind>.<name> for the others. Each test carries two labels: the module label and its kind (host, fail, size,
# check or tool). VHAL_MODULE_LABEL and VHAL_ROOT are set by the root project before the module is added.
#
# vhal_test(<source.cpp> [INCLUDES dirs] [DEFINES defs] [LIBS libs] [OPTIONS flags] [ARGS args] [SOURCES files])
#     builds one host test executable with -std=c++23 -Wall -Wextra -Werror and runs it; the test passes on exit code 0.
#     The include directories are searched in the order given, relative to the module folder. The default is Common. The frame
#     adds .test/include and .test/shim after them, to every host test. A host test that prints the VHAL_SKIP: marker is reported
#     as skipped, with its reason. With --sanitize the test is built and run with the sanitizers (see the root project).
# vhal_test(GLOB <pattern> [INCLUDES dirs] ...)
#     the same for each file of the module folder that matches the pattern, with the same options.
# vhal_fail(<dir> [INCLUDES dirs] [OPTIONS flags])
#     one test per *.cpp file of the folder. Each snippet must fail to compile with -fsyntax-only. Its first line is
#     "// expect: <text>", and its diagnostic must contain that text. A second line "// one error" asks for exactly one error line,
#     and a second line "// expect-value: <text>" asks for the value <text> in the diagnostic, printed as "Value = <text>]" by GCC
#     or as ", <text>>" by Clang.
# vhal_headers(<header>... [INCLUDES dirs] [OPTIONS flags])
#     one test per header, named <label>.check.Header_<file stem>, or Header_<folder>_<file stem> when two headers of the call share
#     the stem: the header is the only include of a translation unit, which must pass -fsyntax-only with -std=c++23 -Wall -Wextra
#     -Werror and the OPTIONS flags. The header is included by its path under the first include root that holds it, and by quoted
#     absolute path when no root holds it. The headers are paths relative to the module folder or absolute.
# vhal_size(<script.cmake>)
#     runs a size measurement with the ARM compiler (VHAL_ARM_CXX). The test is skipped when that compiler is absent or older
#     than GCC 14, or the size tool is absent.
# vhal_check(<script.cmake> [ARGS args])
#     runs a scripted check with cmake -P. The arguments follow the -- on the command line, and the script reads them from
#     CMAKE_ARGV, the last one being the last argument. The script gets the nm tool of the ARM objects as VHAL_ARM_NM.
# vhal_declared(<pattern> <file>...)
#     fails the configure when a file of the module folder matches the pattern and is not one of the declared files.
# vhal_tool_test(<dir> [NAME <name>] [SCRIPT <npm script>] [PREPARE <npm script>])
#     runs the tests of a counterpart program in the .tool folder: npm run test (or SCRIPT) for a package.json, dotnet test for a
#     project file, and the CTest tests of a CMakeLists.txt. The test is skipped when its tool is absent. PREPARE names an npm script
#     that runs before the tests when a g++ is on the PATH (a device simulator is built that way); when it fails, the test is skipped.
#     The test is named <module label>.tool for the module's .tool folder, and <module label>.tool.<sub folder> below it. NAME adds
#     a last part, so that one folder can have a second test, such as the end-to-end suite of a package.json (SCRIPT test:e2e).


# Adds one host test executable for a source file of the module folder, and the test that runs it.
function(vhalAddHostTest source stem)
  cmake_parse_arguments(PARSE_ARGV 2 vhalArg "" "" "INCLUDES;DEFINES;LIBS;OPTIONS;ARGS;SOURCES")

  if(NOT vhalArg_INCLUDES)
    set(vhalArg_INCLUDES "${VHAL_ROOT}/Common")
  endif()

  set(includeDirs "")
  foreach(includeDir IN LISTS vhalArg_INCLUDES)
    cmake_path(ABSOLUTE_PATH includeDir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE absoluteDir)
    list(APPEND includeDirs "${absoluteDir}")
  endforeach()

  set(extraSources "")
  foreach(extraSource IN LISTS vhalArg_SOURCES)
    list(APPEND extraSources "${CMAKE_CURRENT_SOURCE_DIR}/${extraSource}")
  endforeach()

  string(REPLACE "/" "_" flatLabel "${VHAL_MODULE_LABEL}")
  set(targetName "vhaltest_${flatLabel}_${stem}")
  set(testName "${VHAL_MODULE_LABEL}.${stem}")

  add_executable(${targetName} "${CMAKE_CURRENT_SOURCE_DIR}/${source}" ${extraSources})
  target_compile_features(${targetName} PRIVATE cxx_std_23)
  target_compile_options(${targetName} PRIVATE -Wall -Wextra -Werror ${vhalArg_OPTIONS})
  target_compile_definitions(${targetName} PRIVATE ${vhalArg_DEFINES})
  target_include_directories(${targetName} PRIVATE ${includeDirs} "${VHAL_TEST_DIR}/include" "${VHAL_TEST_DIR}/shim")
  target_link_libraries(${targetName} PRIVATE ${vhalArg_LIBS})

  if(VHAL_SANITIZE)
    target_compile_options(${targetName} PRIVATE ${VHAL_SANITIZE_FLAGS})
    target_link_options(${targetName} PRIVATE ${VHAL_SANITIZE_FLAGS})
  endif()

  add_test(NAME "${testName}" COMMAND "$<TARGET_FILE:${targetName}>" ${vhalArg_ARGS})
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};host" SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")

  if(VHAL_SANITIZE)
    set_tests_properties("${testName}" PROPERTIES ENVIRONMENT "${VHAL_SANITIZER_ENVIRONMENT}")
  endif()
endfunction()


function(vhal_test first)
  if(first STREQUAL "GLOB")
    set(restArgs ${ARGN})
    list(POP_FRONT restArgs pattern)

    file(GLOB sources RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${pattern}")
    list(SORT sources)
    foreach(source IN LISTS sources)
      get_filename_component(stem "${source}" NAME_WE)
      vhalAddHostTest("${source}" "${stem}" ${restArgs})
    endforeach()
  else()
    get_filename_component(stem "${first}" NAME_WE)
    vhalAddHostTest("${first}" "${stem}" ${ARGN})
  endif()
endfunction()


# One compile-fail test per snippet of the folder.
function(vhal_fail dir)
  cmake_parse_arguments(PARSE_ARGV 1 vhalArg "" "" "INCLUDES;OPTIONS")

  cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE snippetDir)
  file(GLOB snippets "${snippetDir}/*.cpp")
  list(SORT snippets)

  set(includeDirs "")
  foreach(includeDir IN LISTS vhalArg_INCLUDES)
    cmake_path(ABSOLUTE_PATH includeDir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE absoluteDir)
    list(APPEND includeDirs "${absoluteDir}")
  endforeach()
  if(NOT vhalArg_INCLUDES)
    set(includeDirs "${VHAL_ROOT}/Common")
  endif()

  foreach(snippet IN LISTS snippets)
    get_filename_component(stem "${snippet}" NAME_WE)
    set(testName "${VHAL_MODULE_LABEL}.fail.${stem}")

    add_test(
      NAME "${testName}"
      COMMAND "${CMAKE_COMMAND}"
        "-DVHAL_CXX=${CMAKE_CXX_COMPILER}"
        "-DVHAL_SNIPPET=${snippet}"
        "-DVHAL_INCLUDES=${includeDirs}"
        "-DVHAL_OPTIONS=${vhalArg_OPTIONS}"
        -P "${VHAL_TEST_DIR}/FailCheck.cmake")
    set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};fail")
  endforeach()
endfunction()


# One header-alone test per header. The header is included by the path it has under the first include root that holds it, so
# the test checks the spelling that the consumers use; a header under no root is included by its quoted absolute path. Each header
# is compiled alone by HeaderAlone.cmake, with the flags and the include roots that the module gives.
function(vhal_headers)
  cmake_parse_arguments(PARSE_ARGV 0 vhalArg "" "" "INCLUDES;OPTIONS")

  set(includeDirs "")
  foreach(includeDir IN LISTS vhalArg_INCLUDES)
    cmake_path(ABSOLUTE_PATH includeDir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE absoluteDir)
    list(APPEND includeDirs "${absoluteDir}")
  endforeach()

  # The stems of the headers. A stem that two headers share is given its folder as a prefix, so the test names stay unique.
  set(headerPaths "")
  set(stems "")
  foreach(header IN LISTS vhalArg_UNPARSED_ARGUMENTS)
    cmake_path(ABSOLUTE_PATH header BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE headerPath)
    get_filename_component(headerStem "${headerPath}" NAME_WE)
    list(APPEND headerPaths "${headerPath}")
    list(APPEND stems "${headerStem}")
  endforeach()

  set(names "")
  foreach(headerPath IN LISTS headerPaths)
    get_filename_component(stem "${headerPath}" NAME_WE)

    set(stemCount 0)
    foreach(otherStem IN LISTS stems)
      if(otherStem STREQUAL stem)
        math(EXPR stemCount "${stemCount} + 1")
      endif()
    endforeach()

    set(label "${stem}")
    if(stemCount GREATER 1)
      get_filename_component(folderPath "${headerPath}" DIRECTORY)
      get_filename_component(folderName "${folderPath}" NAME)
      set(label "${folderName}_${stem}")
    endif()
    if(label IN_LIST names)
      message(FATAL_ERROR "vhal_headers: two headers of ${CMAKE_CURRENT_SOURCE_DIR} have the test name ${label}")
    endif()
    list(APPEND names "${label}")

    set(headerInclude "")
    foreach(includeDir IN LISTS includeDirs)
      cmake_path(IS_PREFIX includeDir "${headerPath}" NORMALIZE underRoot)
      if(underRoot AND headerInclude STREQUAL "")
        cmake_path(RELATIVE_PATH headerPath BASE_DIRECTORY "${includeDir}" OUTPUT_VARIABLE headerInclude)
      endif()
    endforeach()

    if(headerInclude STREQUAL "")
      set(probeLine "#include \"${headerPath}\"")
    else()
      set(probeLine "#include <${headerInclude}>")
    endif()

    set(testName "${VHAL_MODULE_LABEL}.check.Header_${label}")
    add_test(
      NAME "${testName}"
      COMMAND "${CMAKE_COMMAND}"
        "-DVHAL_CXX=${CMAKE_CXX_COMPILER}"
        "-DVHAL_PROBE_LINE=${probeLine}"
        "-DVHAL_INCLUDES=${includeDirs}"
        "-DVHAL_OPTIONS=${vhalArg_OPTIONS}"
        "-DVHAL_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/header-${label}"
        -P "${VHAL_TEST_DIR}/HeaderAlone.cmake")
    set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};check")
  endforeach()
endfunction()


# One size measurement. The script is run with the ARM compiler, the size tool, the repository root and a work folder; it prints
# its table, and it prints the VHAL_SKIP marker instead when the toolchain is absent.
function(vhal_size script)
  cmake_path(ABSOLUTE_PATH script BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE scriptPath)
  get_filename_component(stem "${scriptPath}" NAME_WE)
  set(testName "${VHAL_MODULE_LABEL}.size.${stem}")

  add_test(
    NAME "${testName}"
    COMMAND "${CMAKE_COMMAND}"
      "-DVHAL_ARM_CXX=${VHAL_ARM_CXX}"
      "-DVHAL_ARM_SIZE=${VHAL_ARM_SIZE}"
      "-DVHAL_ROOT=${VHAL_ROOT}"
      "-DVHAL_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/size-${stem}"
      -P "${scriptPath}")
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};size" SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")
endfunction()


# One scripted check. The script is run with cmake -P; the test name carries the arguments when there are some. The script gets the
# host compiler as VHAL_CXX and the ARM compiler as VHAL_ARM_CXX (a name or a path, which the script checks before it uses it, as
# VhalSize.cmake does); a script that cannot run prints the VHAL_SKIP: marker with its reason, and the test is reported as skipped.
function(vhal_check script)
  cmake_parse_arguments(PARSE_ARGV 1 vhalArg "" "" "ARGS")

  cmake_path(ABSOLUTE_PATH script BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE scriptPath)
  get_filename_component(stem "${scriptPath}" NAME_WE)

  set(suffix "")
  if(vhalArg_ARGS)
    string(REPLACE ";" "_" suffix "${vhalArg_ARGS}")
    set(suffix "_${suffix}")
  endif()
  set(testName "${VHAL_MODULE_LABEL}.check.${stem}${suffix}")

  add_test(
    NAME "${testName}"
    COMMAND "${CMAKE_COMMAND}"
      "-DVHAL_CXX=${CMAKE_CXX_COMPILER}"
      "-DVHAL_ARM_CXX=${VHAL_ARM_CXX}"
      "-DVHAL_ARM_NM=${VHAL_ARM_NM}"
      "-DVHAL_ROOT=${VHAL_ROOT}"
      "-DVHAL_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/check-${stem}${suffix}"
      -P "${scriptPath}" --
      ${vhalArg_ARGS})
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};check" SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")
endfunction()


# Fails the configure when a file of the module folder that matches the pattern is not one of the declared files. A module that
# declares its tests one by one calls it with the same pattern, so a test file added later cannot be left out without notice.
function(vhal_declared pattern)
  file(GLOB foundFiles RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${pattern}")

  foreach(foundFile IN LISTS foundFiles)
    if(NOT foundFile IN_LIST ARGN)
      message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/${foundFile} matches ${pattern} but is not declared in CMakeLists.txt")
    endif()
  endforeach()
endfunction()


# One counterpart-tool test. A package.json is tested with npm, a project file with dotnet; the tool's own test suite runs.
function(vhal_tool_test dir)
  cmake_parse_arguments(PARSE_ARGV 1 vhalArg "" "NAME;PREPARE;SCRIPT" "")

  cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE toolDir)

  # The folder's place in the module's .tool folder gives the name: the root gives <label>.tool, a sub folder <label>.tool.<path>.
  # NAME adds one more part for a second entry of the same folder, such as <label>.tool.e2e.
  get_filename_component(moduleDir "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
  file(RELATIVE_PATH toolSubPath "${moduleDir}/.tool" "${toolDir}")
  set(toolSuffix "")
  if(NOT toolSubPath STREQUAL "" AND NOT toolSubPath STREQUAL ".")
    string(REPLACE "/" "." toolSuffix ".${toolSubPath}")
  endif()
  if(vhalArg_NAME)
    set(toolSuffix "${toolSuffix}.${vhalArg_NAME}")
  endif()
  set(testName "${VHAL_MODULE_LABEL}.tool${toolSuffix}")

  set(prepareArg "")
  if(vhalArg_PREPARE)
    set(prepareArg "-DVHAL_TOOL_PREPARE=${vhalArg_PREPARE}")
  endif()

  set(scriptArg "")
  if(vhalArg_SCRIPT)
    set(scriptArg "-DVHAL_TOOL_SCRIPT=${vhalArg_SCRIPT}")
  endif()

  add_test(
    NAME "${testName}"
    COMMAND "${CMAKE_COMMAND}"
      "-DVHAL_TOOL_DIR=${toolDir}"
      ${prepareArg}
      ${scriptArg}
      -P "${VHAL_TEST_DIR}/ToolTest.cmake")
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};tool" SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")
endfunction()
