# Checks that each call of Matches, Contains and Find keeps its workspace in its own frame. FrameCaller.cpp makes the six calls of two
# patterns of different sizes from one function, and it is compiled for the device with -O3 and -fstack-usage. The stack usage file then
# has to show:
#   - the caller frame is under 128 bytes, so the caller holds no workspace;
#   - the frames of the two Matches, the two Contains and the two Find are paired by size, the smallest with the smallest, and each frame
#     is at least the workspace of its pattern and at most that workspace plus 256 bytes.
# The lower bound shows the workspace is in the frame. The slack covers the result object and the loop locals that are not workspace:
# the largest one measured for Find is 224 bytes.
# Without the entry points marked noinline, GCC folds the calls into the caller, so the caller frame is over the limit and the Find lines
# are missing; the check then fails. The script prints the VHAL_SKIP marker and passes when the ARM compiler is absent or older than
# GCC 14. The inputs are VHAL_CXX (the host compiler), VHAL_ARM_CXX (the ARM compiler), VHAL_ROOT and VHAL_WORK_DIR.
cmake_minimum_required(VERSION 3.25)

include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(frameArmVersion)
if(frameArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the frame check was not run")
  return()
endif()

set(callerFrameLimitBytes 128)
set(frameSlackBytes 256)
set(frameFailures 0)

if(WIN32)
  set(exeSuffix ".exe")
else()
  set(exeSuffix "")
endif()

set(commonDir "${VHAL_ROOT}/Common")
set(simDir "${CMAKE_CURRENT_LIST_DIR}")

file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

# The workspace sizes come from a host build of the same patterns. They count arrays of 2- and 4-byte elements, so they match the device.
set(sizesExe "${VHAL_WORK_DIR}/FrameSizes${exeSuffix}")
execute_process(
  COMMAND "${VHAL_CXX}" -std=c++23 -Wall -Wextra -Werror -I "${commonDir}" -o "${sizesExe}" "${simDir}/FrameSizes.cpp"
  RESULT_VARIABLE sizesBuild
  ERROR_VARIABLE sizesBuildErr
  OUTPUT_QUIET)
if(NOT sizesBuild EQUAL 0)
  message("${sizesBuildErr}")
  message(FATAL_ERROR "BUILD FAILED: FrameSizes")
endif()

execute_process(
  COMMAND "${sizesExe}"
  RESULT_VARIABLE sizesRun
  OUTPUT_FILE "${VHAL_WORK_DIR}/sizes.txt")
if(NOT sizesRun EQUAL 0)
  message(FATAL_ERROR "RUN FAILED: FrameSizes")
endif()

execute_process(
  COMMAND "${VHAL_ARM_CXX}" -std=gnu++23 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -fno-rtti -fno-exceptions -fdata-sections -ffunction-sections -fstack-usage -O3 -Wall -Wextra -Werror
    -I "${commonDir}"
    -c "${simDir}/FrameCaller.cpp" -o "${VHAL_WORK_DIR}/FrameCaller.o"
  RESULT_VARIABLE callerBuild
  ERROR_VARIABLE callerBuildErr
  OUTPUT_QUIET)
if(NOT callerBuild EQUAL 0)
  message("${callerBuildErr}")
  message(FATAL_ERROR "BUILD FAILED: FrameCaller")
endif()

file(STRINGS "${VHAL_WORK_DIR}/FrameCaller.su" suLines)
file(STRINGS "${VHAL_WORK_DIR}/sizes.txt" sizeLines)


# Sets resultVar to the frame size of one line of the stack usage file: its second tab-separated field.
function(frameSizeOfLine line resultVar)
  string(REGEX REPLACE "^[^\t]*\t([0-9]+)\t.*$" "\\1" size "${line}")
  set(${resultVar} "${size}" PARENT_SCOPE)
endfunction()


# The frame of the caller: the one line of the stack usage file that names FrameCaller.
set(callerFrames "")
foreach(suLine IN LISTS suLines)
  string(FIND "${suLine}" "FrameCaller(" callerPosition)
  if(NOT callerPosition EQUAL -1)
    frameSizeOfLine("${suLine}" callerSize)
    list(APPEND callerFrames "${callerSize}")
  endif()
endforeach()

list(LENGTH callerFrames callerCount)
if(NOT callerCount EQUAL 1)
  message(FATAL_ERROR "CHECK FAILED: FrameCaller is not one function in the stack usage file")
endif()

list(GET callerFrames 0 callerFrame)
if(callerFrame GREATER_EQUAL callerFrameLimitBytes)
  message("CHECK FAILED: caller frame ${callerFrame} bytes, limit ${callerFrameLimitBytes} (a workspace is in the caller)")
  math(EXPR frameFailures "${frameFailures} + 1")
else()
  message("caller frame ${callerFrame} bytes, limit ${callerFrameLimitBytes}")
endif()


# Compares the frames of one entry point with the workspace sizes of one kind, smallest with smallest. The result is set in
# frameKindFailed of the caller when a frame is out of its range.
function(frameCheckKind entry kind)
  set(frames "")
  foreach(suLine IN LISTS suLines)
    string(FIND "${suLine}" "${entry}" entryPosition)
    if(NOT entryPosition EQUAL -1)
      frameSizeOfLine("${suLine}" frameSize)
      list(APPEND frames "${frameSize}")
    endif()
  endforeach()

  set(workspaces "")
  foreach(sizeLine IN LISTS sizeLines)
    if(sizeLine MATCHES "^${kind} ([0-9]+)$")
      list(APPEND workspaces "${CMAKE_MATCH_1}")
    endif()
  endforeach()

  list(LENGTH frames frameCount)
  list(LENGTH workspaces workspaceCount)
  if(NOT frameCount EQUAL 2 OR NOT workspaceCount EQUAL 2)
    message("CHECK FAILED: ${entry} has ${frameCount} frames, expected 2")
    set(frameKindFailed ON PARENT_SCOPE)
    return()
  endif()

  list(GET frames 0 firstFrame)
  list(GET frames 1 secondFrame)
  if(firstFrame GREATER secondFrame)
    set(sortedFrames "${secondFrame};${firstFrame}")
  else()
    set(sortedFrames "${firstFrame};${secondFrame}")
  endif()

  list(GET workspaces 0 firstWorkspace)
  list(GET workspaces 1 secondWorkspace)
  if(firstWorkspace GREATER secondWorkspace)
    set(sortedWorkspaces "${secondWorkspace};${firstWorkspace}")
  else()
    set(sortedWorkspaces "${firstWorkspace};${secondWorkspace}")
  endif()

  set(kindFailed OFF)
  foreach(index RANGE 0 1)
    list(GET sortedFrames ${index} frame)
    list(GET sortedWorkspaces ${index} workspace)
    math(EXPR limit "${workspace} + ${frameSlackBytes}")
    if(frame LESS workspace OR frame GREATER limit)
      message("CHECK FAILED: ${entry} frame ${frame} bytes, workspace ${workspace}, allowed ${workspace} to ${limit}")
      set(kindFailed ON)
    else()
      message("${entry} frame ${frame} bytes, workspace ${workspace}, allowed ${workspace} to ${limit}")
    endif()
  endforeach()

  if(kindFailed)
    set(frameKindFailed ON PARENT_SCOPE)
  endif()
endfunction()


set(frameKindFailed OFF)
frameCheckKind("::Matches(" plain)
if(frameKindFailed)
  math(EXPR frameFailures "${frameFailures} + 1")
endif()

set(frameKindFailed OFF)
frameCheckKind("::Contains(" plain)
if(frameKindFailed)
  math(EXPR frameFailures "${frameFailures} + 1")
endif()

set(frameKindFailed OFF)
frameCheckKind("::Find(" captures)
if(frameKindFailed)
  math(EXPR frameFailures "${frameFailures} + 1")
endif()

if(frameFailures GREATER 0)
  message(FATAL_ERROR "frame check failed")
endif()

message("frame check passed")
