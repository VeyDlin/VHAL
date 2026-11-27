# Runs the tests of a counterpart tool of a module's .tool folder: npm test when the folder has a package.json, dotnet test when it
# has a project or solution file. The script prints the VHAL_SKIP marker and passes when the tool is absent, and fails when the
# tests fail. The input is VHAL_TOOL_DIR.
cmake_minimum_required(VERSION 3.25)

if(EXISTS "${VHAL_TOOL_DIR}/package.json")
  find_program(vhalNpm NAMES npm npm.cmd)
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

message(FATAL_ERROR "${VHAL_TOOL_DIR} has no package.json, project or solution file to test")
