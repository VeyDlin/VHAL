# Runs the tests of a counterpart tool of a module's .tool folder. The input is VHAL_TOOL_DIR. The tool kind is found from its files:
#
# - a package.json: npm test, skipped when npm is absent. The script never runs npm ci or any download.
# - a project or solution file: dotnet test, skipped when dotnet is absent.
# - a CMakeLists.txt: the tool is configured and built in .test-build/tools/<folder path>, with the generator and the host compiler
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

  execute_process(
    COMMAND "${vhalNpm}" test
    WORKING_DIRECTORY "${VHAL_TOOL_DIR}"
    RESULT_VARIABLE testResult)
  if(NOT testResult EQUAL 0)
    message(FATAL_ERROR "npm test failed in ${VHAL_TOOL_DIR} (${testResult})")
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
  set(vhalToolBuild "${vhalToolRoot}/.test-build/tools/${vhalToolKey}")

  # The main build is configured by Run.cmake before any test runs; its cache names the generator and the host compiler.
  set(vhalCache "${vhalToolRoot}/.test-build/CMakeCache.txt")
  if(NOT EXISTS "${vhalCache}")
    message(FATAL_ERROR "The build folder .test-build is not configured: run ./test.cmd first")
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
  return()
endif()

message(FATAL_ERROR "${VHAL_TOOL_DIR} has no package.json, project or solution file, and no CMakeLists.txt, to test")
