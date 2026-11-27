# Checks that an unused CRC preset costs nothing. A Cortex-M4 image that includes all the preset files and uses one of them must be the
# same size, in text and in RAM, as the image that includes only that one file. Both are built at -Os and -O2 with the ARM compiler
# and linked without the C library, so the sizes are only the code and data of the CRC call. The script fails when the sizes differ
# or a file does not build. It prints the VHAL_SKIP marker and passes when the ARM compiler is absent or older than GCC 14.
cmake_minimum_required(VERSION 3.25)

set(vhalSizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(vhalArmVersion)
if(vhalArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the preset size check was not run")
  return()
endif()

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(presetFlags
  -std=c++23 -mcpu=cortex-m4 -mthumb -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
set(presetIncludes -I "${VHAL_ROOT}/Common")


# Builds and links one image for an optimisation, and sets resultVar to "text ram" of the program (RAM is data plus bss), or to
# "failed" when a step fails.
function(presetMeasure resultVar opt source name)
  get_filename_component(sourceName "${source}" NAME)
  set(object "${VHAL_WORK_DIR}/${name}-${opt}.o")
  set(image "${VHAL_WORK_DIR}/${name}-${opt}.elf")

  execute_process(
    COMMAND "${VHAL_ARM_CXX}" ${presetFlags} "-${opt}" ${presetIncludes} -c -o "${object}" "${source}"
    RESULT_VARIABLE objectResult
    ERROR_VARIABLE objectErr
    OUTPUT_QUIET)
  if(NOT objectResult EQUAL 0)
    message("${objectErr}")
    message("BUILD FAILED: ${sourceName} at -${opt}")
    set(${resultVar} "failed" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND "${VHAL_ARM_CXX}" -mcpu=cortex-m4 -mthumb "-${opt}" -nostdlib -Wl,--gc-sections -Wl,-e,SizePresetsEntry
      -o "${image}" "${object}"
    RESULT_VARIABLE linkResult
    ERROR_VARIABLE linkErr
    OUTPUT_QUIET)
  if(NOT linkResult EQUAL 0)
    message("${linkErr}")
    message("LINK FAILED: ${sourceName} at -${opt}")
    set(${resultVar} "failed" PARENT_SCOPE)
    return()
  endif()

  vhalSizeImage(measured "${image}")
  set(${resultVar} "${measured}" PARENT_SCOPE)
endfunction()


set(presetFailures 0)

message("")
message("| Optimisation | Image | M4 text | M4 RAM |")
message("|---|---|---:|---:|")

foreach(opt IN ITEMS Os O2)
  presetMeasure(one "${opt}" "${vhalSizeDir}/SizePresetsOne.cpp" one)
  if(one STREQUAL "failed")
    math(EXPR presetFailures "${presetFailures} + 1")
    continue()
  endif()

  presetMeasure(all "${opt}" "${vhalSizeDir}/SizePresetsAll.cpp" all)
  if(all STREQUAL "failed")
    math(EXPR presetFailures "${presetFailures} + 1")
    continue()
  endif()

  string(REPLACE " " ";" oneParts "${one}")
  list(GET oneParts 0 oneText)
  list(GET oneParts 1 oneRam)
  string(REPLACE " " ";" allParts "${all}")
  list(GET allParts 0 allText)
  list(GET allParts 1 allRam)

  message("| -${opt} | one preset file | ${oneText} | ${oneRam} |")
  message("| -${opt} | all preset files, one used | ${allText} | ${allRam} |")

  if(NOT oneText EQUAL allText OR NOT oneRam EQUAL allRam)
    message("PRESETS COST: -${opt} image with all presets is ${allText} text and ${allRam} RAM, with one file ${oneText} and ${oneRam}")
    math(EXPR presetFailures "${presetFailures} + 1")
  endif()
endforeach()

message("")
execute_process(
  COMMAND "${VHAL_ARM_CXX}" --version
  OUTPUT_VARIABLE armVersionOut
  ERROR_QUIET)
string(REPLACE "\r" "" armVersionOut "${armVersionOut}")
string(REGEX MATCH "^[^\n]*" armVersionLine "${armVersionOut}")
message("${armVersionLine}")

if(presetFailures GREATER 0)
  message(FATAL_ERROR "${presetFailures} preset size check(s) failed")
endif()
