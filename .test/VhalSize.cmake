# Shared by the size measurements of the modules. A size script includes this file, sets vhalSizeDir to its own folder and calls
# vhalSizeToolchainVersion first. vhal_size runs the script with VHAL_ARM_CXX (the ARM compiler), VHAL_ARM_SIZE (the size tool),
# VHAL_ROOT (the repository root) and VHAL_WORK_DIR (a folder for the objects and images, emptied by the script).
cmake_minimum_required(VERSION 3.25)

set(vhalSizeFlags
  -std=c++23 -mthumb -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
set(vhalSizeLinkFlags -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs)


# Sets resultVar to the GCC version of the ARM compiler, or to an empty string when the compiler is absent or older than GCC 14.
function(vhalSizeToolchainVersion resultVar)
  execute_process(
    COMMAND "${VHAL_ARM_CXX}" -dumpfullversion
    RESULT_VARIABLE versionResult
    OUTPUT_VARIABLE version
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(result "")
  if(versionResult EQUAL 0 AND version MATCHES "^[0-9]+")
    string(REGEX MATCH "^[0-9]+" major "${version}")
    if(NOT major LESS 14)
      set(result "${version}")
    endif()
  endif()

  set(${resultVar} "${result}" PARENT_SCOPE)
endfunction()


# Builds and links one image for the ARM target: the main of the optimisation and CPU, and the source with the defines given after
# it. Sets resultVar to "text ram" (the text, and the data plus bss, of the linked image as arm-none-eabi-size reads it), or to
# "failed" when a step fails. The compiler output of a failed step is printed.
function(vhalSizeProgram resultVar opt cpu name source)
  set(mainObject "${VHAL_WORK_DIR}/main-${opt}-${cpu}.o")
  set(objectFile "${VHAL_WORK_DIR}/${name}.o")
  set(image "${VHAL_WORK_DIR}/${name}.elf")

  execute_process(
    COMMAND "${VHAL_ARM_CXX}" ${vhalSizeFlags} "-${opt}" "-mcpu=${cpu}" ${vhalSizeIncludes}
      -c -o "${mainObject}" "${vhalSizeDir}/SizeMain.cpp"
    RESULT_VARIABLE mainResult
    ERROR_VARIABLE mainErr
    OUTPUT_QUIET)
  if(NOT mainResult EQUAL 0)
    message("BUILD FAILED: SizeMain.cpp for -${opt}, ${cpu}")
    message("${mainErr}")
    set(${resultVar} "failed" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND "${VHAL_ARM_CXX}" ${vhalSizeFlags} "-${opt}" "-mcpu=${cpu}" ${vhalSizeIncludes} ${ARGN}
      -c -o "${objectFile}" "${source}"
    RESULT_VARIABLE objectResult
    ERROR_VARIABLE objectErr
    OUTPUT_QUIET)
  if(NOT objectResult EQUAL 0)
    message("BUILD FAILED: ${name} for -${opt}, ${cpu}")
    message("${objectErr}")
    set(${resultVar} "failed" PARENT_SCOPE)
    return()
  endif()

  # The nosys and nano stubs of the C library warn that they are not implemented; the warnings are not printed.
  execute_process(
    COMMAND "${VHAL_ARM_CXX}" -mthumb "-${opt}" "-mcpu=${cpu}" "${mainObject}" "${objectFile}" -o "${image}" ${vhalSizeLinkFlags}
    RESULT_VARIABLE linkResult
    ERROR_VARIABLE linkErr
    OUTPUT_QUIET)
  if(NOT linkResult EQUAL 0)
    message("${linkErr}")
    message("LINK FAILED: ${name}")
    set(${resultVar} "failed" PARENT_SCOPE)
    return()
  endif()

  vhalSizeImage(measured "${image}")
  set(${resultVar} "${measured}" PARENT_SCOPE)
endfunction()


# Reads the text and the RAM (data plus bss) of a linked image with the size tool. Sets resultVar to "text ram". A failure of the size
# tool stops the script, because a missing reading would make the table wrong.
function(vhalSizeImage resultVar image)
  execute_process(
    COMMAND "${VHAL_ARM_SIZE}" --format=berkeley "${image}"
    RESULT_VARIABLE sizeResult
    OUTPUT_VARIABLE sizeOut
    ERROR_QUIET)
  if(NOT sizeResult EQUAL 0)
    message(FATAL_ERROR "${VHAL_ARM_SIZE} could not read ${image}")
  endif()

  string(REPLACE "\r" "" sizeOut "${sizeOut}")
  string(STRIP "${sizeOut}" sizeOut)
  string(REGEX MATCH "[^\n]*$" lastLine "${sizeOut}")
  string(REGEX MATCHALL "[0-9]+" numbers "${lastLine}")
  list(GET numbers 0 text)
  list(GET numbers 1 data)
  list(GET numbers 2 bss)
  math(EXPR ram "${data} + ${bss}")

  set(${resultVar} "${text} ${ram}" PARENT_SCOPE)
endfunction()
