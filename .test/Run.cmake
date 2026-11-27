# Runs the VHAL tests. test.cmd calls this script with cmake -P and passes its own arguments after the --.
#
# Usage: test.cmd [module labels] [-L kind] [-j N] [-V] [--list]
#   A word that does not start with '-' names modules: each one is a label, and the tests with any of the labels run.
#   Anything that starts with '-' goes to CTest as it is. --list prints the tests and runs nothing.
#
# The compiler is CXX when it is set, otherwise g++. It must be GCC 14 or newer. The build folder .test-build is configured
# once (Ninja when it is found), every test executable is built with -k so that one failing target does not hide the others,
# and CTest then runs the selected tests. The script fails when the configure, the build or a test fails.

cmake_minimum_required(VERSION 3.25)

get_filename_component(vhalTestDir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(vhalRoot "${vhalTestDir}/.." ABSOLUTE)
set(vhalBuildDir "${vhalRoot}/.test-build")

if(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
  set(vhalCxx "$ENV{CXX}")
else()
  set(vhalCxx "g++")
endif()

if(DEFINED ENV{ARM_CXX} AND NOT "$ENV{ARM_CXX}" STREQUAL "")
  set(vhalArmCxx "$ENV{ARM_CXX}")
else()
  set(vhalArmCxx "arm-none-eabi-g++")
endif()

if(DEFINED ENV{ARM_SIZE} AND NOT "$ENV{ARM_SIZE}" STREQUAL "")
  set(vhalArmSize "$ENV{ARM_SIZE}")
else()
  set(vhalArmSize "arm-none-eabi-size")
endif()

# The host compiler must report its version, and the version must be GCC 14 or newer.
execute_process(
  COMMAND "${vhalCxx}" -dumpfullversion
  RESULT_VARIABLE vhalCxxResult
  OUTPUT_VARIABLE vhalCxxVersion
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT vhalCxxResult EQUAL 0 OR NOT vhalCxxVersion MATCHES "^[0-9]+")
  message(FATAL_ERROR "The compiler \"${vhalCxx}\" does not report a GCC version with -dumpfullversion. Set CXX to a g++ of GCC 14 or newer.")
endif()

string(REGEX MATCH "^[0-9]+" vhalCxxMajor "${vhalCxxVersion}")
if(vhalCxxMajor LESS 14)
  message(FATAL_ERROR "The compiler \"${vhalCxx}\" is GCC ${vhalCxxVersion}. GCC 14 or newer is required. Set CXX to a g++ of GCC 14 or newer.")
endif()

# The generator is chosen once, when the build folder is first configured. Ninja is used when it is on the PATH.
set(vhalGeneratorArgs "")
if(NOT EXISTS "${vhalBuildDir}/CMakeCache.txt")
  find_program(vhalNinja ninja)
  if(vhalNinja)
    list(APPEND vhalGeneratorArgs -G Ninja)
  elseif(WIN32)
    list(APPEND vhalGeneratorArgs -G "MinGW Makefiles")
  else()
    list(APPEND vhalGeneratorArgs -G "Unix Makefiles")
  endif()
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${vhalGeneratorArgs}
    -S "${vhalTestDir}"
    -B "${vhalBuildDir}"
    "-DCMAKE_CXX_COMPILER=${vhalCxx}"
    "-DVHAL_ARM_CXX=${vhalArmCxx}"
    "-DVHAL_ARM_SIZE=${vhalArmSize}"
  RESULT_VARIABLE vhalConfigureResult)

if(NOT vhalConfigureResult EQUAL 0)
  message(FATAL_ERROR "The configure step failed. See the messages above.")
endif()

# The arguments. CMAKE_ARGV0 to CMAKE_ARGV3 are cmake, -P, the script and the --; the test.cmd arguments follow.
set(vhalModules "")
set(vhalCtestArgs "")
set(vhalJobs "")
set(vhalList OFF)
set(vhalPendingOption "")
set(vhalValueOptions -j -L -R -E -I -O --timeout)

math(EXPR vhalLastIndex "${CMAKE_ARGC} - 1")
set(vhalIndex 4)
while(vhalIndex LESS_EQUAL vhalLastIndex)
  set(vhalArg "${CMAKE_ARGV${vhalIndex}}")

  if(vhalPendingOption)
    list(APPEND vhalCtestArgs "${vhalArg}")
    if(vhalPendingOption STREQUAL "-j")
      set(vhalJobs "${vhalArg}")
    endif()
    set(vhalPendingOption "")
  elseif(vhalArg STREQUAL "--list")
    set(vhalList ON)
  elseif(vhalArg MATCHES "^-j([0-9]+)$")
    set(vhalJobs "${CMAKE_MATCH_1}")
    list(APPEND vhalCtestArgs "${vhalArg}")
  elseif(vhalArg MATCHES "^-")
    list(APPEND vhalCtestArgs "${vhalArg}")
    if(vhalArg IN_LIST vhalValueOptions)
      set(vhalPendingOption "${vhalArg}")
    endif()
  elseif(NOT vhalArg STREQUAL "")
    list(APPEND vhalModules "${vhalArg}")
  endif()

  math(EXPR vhalIndex "${vhalIndex} + 1")
endwhile()

if(vhalModules)
  string(REPLACE ";" "|" vhalModulePattern "${vhalModules}")
  list(APPEND vhalCtestArgs -L "^(${vhalModulePattern})$")
endif()

cmake_host_system_information(RESULT vhalCores QUERY NUMBER_OF_LOGICAL_CORES)
if(vhalJobs STREQUAL "")
  set(vhalJobs "${vhalCores}")
endif()

# CTest lists the tests that the arguments select. Its listing also reports each executable that is not built yet, so only the
# test lines and the total are kept.
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${vhalBuildDir}" -N ${vhalCtestArgs}
  RESULT_VARIABLE vhalSelectResult
  OUTPUT_VARIABLE vhalSelectOutput
  ERROR_QUIET)

if(NOT vhalSelectResult EQUAL 0)
  message(FATAL_ERROR "CTest could not list the tests.")
endif()

string(REGEX MATCH "Total Tests: ([0-9]+)" vhalTotalLine "${vhalSelectOutput}")
set(vhalTestCount "${CMAKE_MATCH_1}")
if(vhalTestCount STREQUAL "" OR vhalTestCount EQUAL 0)
  message(FATAL_ERROR "No tests were found for the arguments. Run ./test.cmd --list to see the test names and labels.")
endif()

if(vhalList)
  string(REGEX MATCHALL "[^\n]*(  Test +#|Total Tests:)[^\n]*" vhalSelectLines "${vhalSelectOutput}")
  string(REPLACE ";" "\n" vhalSelectText "${vhalSelectLines}")
  message("${vhalSelectText}")
  return()
endif()

# The build keeps going past a failing target, so a build error in one module does not hide the tests of the others.
file(STRINGS "${vhalBuildDir}/CMakeCache.txt" vhalGeneratorLine REGEX "^CMAKE_GENERATOR:INTERNAL=")
if(vhalGeneratorLine MATCHES "Ninja")
  set(vhalKeepGoing "-k" "0")
else()
  set(vhalKeepGoing "-k")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${vhalBuildDir}" --parallel "${vhalJobs}" -- ${vhalKeepGoing}
  RESULT_VARIABLE vhalBuildResult)

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${vhalBuildDir}" --output-on-failure --no-tests=error ${vhalCtestArgs}
  RESULT_VARIABLE vhalTestResult)

# With a module filter, a build error in a module that is not selected does not count: a selected test whose executable is
# missing fails by itself. Without a filter every target is needed, so a build error fails the run.
if(NOT vhalBuildResult EQUAL 0 AND vhalModules)
  message("The build reported errors (${vhalBuildResult}). Only the selected tests decide the result: a test whose executable is missing is Not Run.")
endif()

if(NOT vhalTestResult EQUAL 0 OR (NOT vhalBuildResult EQUAL 0 AND NOT vhalModules))
  message(FATAL_ERROR "The build (${vhalBuildResult}) or a test (${vhalTestResult}) failed. The failing tests are listed above.")
endif()
