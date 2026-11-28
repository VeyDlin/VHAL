# Runs the tests of a counterpart tool of a module's .tool folder. The inputs are VHAL_TOOL_DIR and, optionally, VHAL_TOOL_SCRIPT and
# VHAL_TOOL_PREPARE. The tool kind is found from its files:
#
# - a package.json: npm run <VHAL_TOOL_SCRIPT, default test>, skipped when npm is absent. The script never runs npm ci or any download.
#   When VHAL_TOOL_PREPARE names an npm script, that script runs first, when a g++ is on the PATH (the tests of a device simulator need
#   it). When that script fails, the test fails: a g++ is present, so the failure is an error of the code it builds.
# - a project or solution file: dotnet test, skipped when dotnet is absent.
# - a CMakeLists.txt: the tool is configured and built in <VHAL_BUILD_DIR>/tools/<folder path>, with the generator and the host compiler
#   of the main build (read from its cache). The tool is built with VHAL_TOOL_TEST set to ON, so that it never downloads a library
#   and prints VHAL_SKIP: when a library it needs is missing.
#
# A skipped tool prints the VHAL_SKIP: marker with its reason and passes. A failing test or build fails the script.
cmake_minimum_required(VERSION 3.25)

set(vhalToolRoot "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(vhalToolRoot "${vhalToolRoot}" ABSOLUTE)

if(EXISTS "${VHAL_TOOL_DIR}/package.json")
  find_program(vhalNpm NAMES npm.cmd npm)
  if(NOT vhalNpm)
    message("VHAL_SKIP: npm is not on the PATH: the tests of ${VHAL_TOOL_DIR} were not run")
    return()
  endif()

  # The npm script that prepares the tests (a device simulator, for one) runs when a g++ is on the PATH, because the simulator is built
  # with it. Without one the tests that need the simulator skip themselves. When the script fails with a g++ present, the code it
  # builds has an error, so the test fails; the compiler's errors are in the log above the message.
  if(VHAL_TOOL_PREPARE)
    find_program(vhalGxx NAMES g++ g++.exe)
    if(vhalGxx)
      execute_process(
        COMMAND "${vhalNpm}" run "${VHAL_TOOL_PREPARE}"
        WORKING_DIRECTORY "${VHAL_TOOL_DIR}"
        RESULT_VARIABLE prepareResult)
      if(NOT prepareResult EQUAL 0)
        message(FATAL_ERROR "npm run ${VHAL_TOOL_PREPARE} failed in ${VHAL_TOOL_DIR} (${prepareResult})")
      endif()
    endif()
  endif()

  if(VHAL_TOOL_SCRIPT)
    set(vhalScript "${VHAL_TOOL_SCRIPT}")
  else()
    set(vhalScript "test")
  endif()

  execute_process(
    COMMAND "${vhalNpm}" run "${vhalScript}"
    WORKING_DIRECTORY "${VHAL_TOOL_DIR}"
    RESULT_VARIABLE testResult)
  if(NOT testResult EQUAL 0)
    message(FATAL_ERROR "npm run ${vhalScript} failed in ${VHAL_TOOL_DIR} (${testResult})")
  endif()
  return()
endif()

file(GLOB projects "${VHAL_TOOL_DIR}/*.csproj" "${VHAL_TOOL_DIR}/*.sln")
if(projects)
  find_program(vhalDotnet dotnet)
  if(NOT vhalDotnet)
    message("VHAL_SKIP: dotnet is not on the PATH: the tests of ${VHAL_TOOL_DIR} were not run")
    return()
  endif()

  list(GET projects 0 project)
  execute_process(
    COMMAND "${vhalDotnet}" test "${project}"
    RESULT_VARIABLE testResult)
  if(NOT testResult EQUAL 0)
    message(FATAL_ERROR "dotnet test failed for ${project} (${testResult})")
  endif()
  return()
endif()

if(EXISTS "${VHAL_TOOL_DIR}/CMakeLists.txt")
  file(RELATIVE_PATH vhalToolPath "${vhalToolRoot}" "${VHAL_TOOL_DIR}")
  string(REPLACE "/" "_" vhalToolKey "${vhalToolPath}")
  set(vhalToolBuild "${VHAL_BUILD_DIR}/tools/${vhalToolKey}")

  # The main build is configured by Run.cmake before any test runs; its cache names the generator and the host compiler.
  set(vhalCache "${VHAL_BUILD_DIR}/CMakeCache.txt")
  if(NOT EXISTS "${vhalCache}")
    message(FATAL_ERROR "The build folder ${VHAL_BUILD_DIR} is not configured: run ./test.cmd first")
  endif()

  file(STRINGS "${vhalCache}" vhalGeneratorLine REGEX "^CMAKE_GENERATOR:INTERNAL=")
  file(STRINGS "${vhalCache}" vhalCompilerLine REGEX "^CMAKE_CXX_COMPILER:")
  string(REGEX REPLACE "^CMAKE_GENERATOR:INTERNAL=" "" vhalGenerator "${vhalGeneratorLine}")
  string(REGEX REPLACE "^CMAKE_CXX_COMPILER:[A-Z]+=" "" vhalCompiler "${vhalCompilerLine}")

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -G "${vhalGenerator}"
      -S "${VHAL_TOOL_DIR}"
      -B "${vhalToolBuild}"
      "-DCMAKE_CXX_COMPILER=${vhalCompiler}"
      -DVHAL_TOOL_TEST=ON
    RESULT_VARIABLE configResult
    OUTPUT_VARIABLE configOut
    ERROR_VARIABLE configErr)

  string(REGEX MATCHALL "VHAL_SKIP:[^\n]*" skipLines "${configOut}${configErr}")
  if(skipLines)
    list(GET skipLines 0 skipLine)
    string(REPLACE "\r" "" skipLine "${skipLine}")
    message("${skipLine}")
    return()
  endif()

  if(NOT configResult EQUAL 0)
    message("${configOut}${configErr}")
    message(FATAL_ERROR "The configure of ${VHAL_TOOL_DIR} failed (${configResult})")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${vhalToolBuild}"
    RESULT_VARIABLE buildResult
    OUTPUT_VARIABLE buildOut
    ERROR_VARIABLE buildErr)
  if(NOT buildResult EQUAL 0)
    message("${buildOut}${buildErr}")
    message(FATAL_ERROR "The build of ${VHAL_TOOL_DIR} failed (${buildResult})")
  endif()

  # The tool's own CTest tests run here: the offline set, which the tool's CMakeLists registers (a test that needs a device is not
  # registered there).
  execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
    WORKING_DIRECTORY "${vhalToolBuild}"
    RESULT_VARIABLE ctestResult)
  if(NOT ctestResult EQUAL 0)
    message(FATAL_ERROR "The tests of ${VHAL_TOOL_DIR} failed (${ctestResult})")
  endif()
  return()
endif()

message(FATAL_ERROR "${VHAL_TOOL_DIR} has no package.json, project or solution file, and no CMakeLists.txt, to test")
