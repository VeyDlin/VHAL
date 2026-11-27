# The functions a module's .test/CMakeLists.txt calls to declare its checks. Each test is named <module label>.<name> for a host test
# and <module label>.<kind>.<name> for the others. Each test carries two labels: the module label and its kind (host, fail, size,
# check or tool). VHAL_MODULE_LABEL and VHAL_ROOT are set by the root project before the module is added.
#
# vhal_test(<source.cpp> [INCLUDES dirs] [DEFINES defs] [LIBS libs] [OPTIONS flags] [ARGS args] [SOURCES files])
#     builds one host test executable with -std=c++23 -Wall -Wextra -Werror and runs it; the test passes on exit code 0.
#     The include directories are searched in the order given, relative to the module folder. The default is Common.
# vhal_test(GLOB <pattern> [INCLUDES dirs] ...)
#     the same for each file of the module folder that matches the pattern, with the same options.
# vhal_fail(<dir> [INCLUDES dirs])
#     one test per *.cpp file of the folder. Each snippet must fail to compile with -fsyntax-only. Its first line is
#     "// expect: <text>", and its diagnostic must contain that text. A second line "// one error" asks for exactly one error line.
# vhal_size(<script.cmake>)
#     runs a size measurement with the ARM compiler (VHAL_ARM_CXX). The test is skipped when that compiler is absent or older
#     than GCC 14, or the size tool is absent.
# vhal_check(<script.cmake> [ARGS args])
#     runs a scripted check with cmake -P. The arguments follow the -- on the command line, and the script reads them from
#     CMAKE_ARGV, the last one being the last argument.
# vhal_declared(<pattern> <file>...)
#     fails the configure when a file of the module folder matches the pattern and is not one of the declared files.
# vhal_tool_test(<dir>)
#     runs the tests of a counterpart program in the .tool folder: npm test for a package.json, dotnet test for a project file.
#     The test is skipped when its tool is absent.


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
  target_include_directories(${targetName} PRIVATE ${includeDirs})
  target_link_libraries(${targetName} PRIVATE ${vhalArg_LIBS})

  add_test(NAME "${testName}" COMMAND "$<TARGET_FILE:${targetName}>" ${vhalArg_ARGS})
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};host")
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
  cmake_parse_arguments(PARSE_ARGV 1 vhalArg "" "" "INCLUDES")

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
        -P "${VHAL_TEST_DIR}/FailCheck.cmake")
    set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};fail")
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


# One scripted check. The script is run with cmake -P; the test name carries the arguments when there are some.
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
      "-DVHAL_ROOT=${VHAL_ROOT}"
      "-DVHAL_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/check-${stem}${suffix}"
      -P "${scriptPath}" --
      ${vhalArg_ARGS})
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};check")
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
  cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE toolDir)
  get_filename_component(stem "${toolDir}" NAME)
  set(testName "${VHAL_MODULE_LABEL}.tool.${stem}")

  add_test(
    NAME "${testName}"
    COMMAND "${CMAKE_COMMAND}"
      "-DVHAL_TOOL_DIR=${toolDir}"
      -P "${VHAL_TEST_DIR}/ToolTest.cmake")
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};tool" SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")
endfunction()
