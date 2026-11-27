# Measures what each CRC engine costs on a Cortex-M4 and a Cortex-M0+, in bytes, for one algorithm (CRC-16/MODBUS) and for three
# (CRC-16/MODBUS, CRC-32/ISO-HDLC and CRC-8/SMBUS), at -Os and -O2. Each cost is a program with the engine's calls minus the
# baseline program, which makes the same call with no CRC, so the table gives the cost of the engine and not of the surrounding
# code. Text is the code and the flash tables; RAM is data plus bss, and the engines keep no RAM, so it is 0 in every row.
#
# The bitwise engine is also measured at -O3: a 1 KB table would be the compiler's -foptimize-crc, and the limit catches it.
# The table is compared with the limits written below. The script fails when a cost grows above its limit, when a row has RAM,
# and when a program does not build. It prints a markdown table and then the toolchain. It prints the VHAL_SKIP marker and passes
# when the ARM compiler is absent or older than GCC 14.
cmake_minimum_required(VERSION 3.25)

set(vhalSizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(vhalArmVersion)
if(vhalArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the CRC size table was not measured")
  return()
endif()

set(vhalSizeIncludes -I "${vhalSizeDir}" -I "${VHAL_ROOT}/Common")

# The text limits, in bytes of cost. The bitwise limits are the measured costs of the engine.
set(vhalSizeLimit_Os_CrcBitwise_1_cortex_m4 24)
set(vhalSizeLimit_Os_CrcBitwise_1_cortex_m0plus 24)
set(vhalSizeLimit_Os_CrcBitwise_3_cortex_m4 112)
set(vhalSizeLimit_Os_CrcBitwise_3_cortex_m0plus 116)
set(vhalSizeLimit_Os_CrcNibble_1_cortex_m4 68)
set(vhalSizeLimit_Os_CrcNibble_1_cortex_m0plus 64)
set(vhalSizeLimit_Os_CrcNibble_3_cortex_m4 260)
set(vhalSizeLimit_Os_CrcNibble_3_cortex_m0plus 264)
set(vhalSizeLimit_Os_CrcTable_1_cortex_m4 528)
set(vhalSizeLimit_Os_CrcTable_1_cortex_m0plus 528)
set(vhalSizeLimit_Os_CrcTable_3_cortex_m4 1876)
set(vhalSizeLimit_Os_CrcTable_3_cortex_m0plus 1876)
set(vhalSizeLimit_O2_CrcBitwise_1_cortex_m4 36)
set(vhalSizeLimit_O2_CrcBitwise_1_cortex_m0plus 24)
set(vhalSizeLimit_O2_CrcBitwise_3_cortex_m4 124)
set(vhalSizeLimit_O2_CrcBitwise_3_cortex_m0plus 120)
set(vhalSizeLimit_O2_CrcNibble_1_cortex_m4 76)
set(vhalSizeLimit_O2_CrcNibble_1_cortex_m0plus 68)
set(vhalSizeLimit_O2_CrcNibble_3_cortex_m4 268)
set(vhalSizeLimit_O2_CrcNibble_3_cortex_m0plus 280)
set(vhalSizeLimit_O2_CrcTable_1_cortex_m4 532)
set(vhalSizeLimit_O2_CrcTable_1_cortex_m0plus 528)
set(vhalSizeLimit_O2_CrcTable_3_cortex_m4 1884)
set(vhalSizeLimit_O2_CrcTable_3_cortex_m0plus 1872)
set(vhalSizeLimit_O3_CrcBitwise_1_cortex_m4 36)
set(vhalSizeLimit_O3_CrcBitwise_1_cortex_m0plus 24)
set(vhalSizeLimit_O3_CrcBitwise_3_cortex_m4 124)
set(vhalSizeLimit_O3_CrcBitwise_3_cortex_m0plus 120)

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")


# The cost of one row for one CPU: the text and the RAM of the program minus those of the baseline of the same optimisation and
# CPU. Sets resultVar to "text ram", or to "failed" when a program does not build.
function(vhalSizeCost resultVar opt cpu engine algorithms)
  vhalSizeProgram(baseline "${opt}" "${cpu}" "baseline-${opt}-${cpu}" "${vhalSizeDir}/SizeBaseline.cpp")
  if(baseline STREQUAL "failed")
    set(${resultVar} "failed" PARENT_SCOPE)
    return()
  endif()

  vhalSizeProgram(
    program
    "${opt}"
    "${cpu}"
    "${opt}-${cpu}-${engine}-${algorithms}"
    "${vhalSizeDir}/SizeConfig.cpp"
    "-DSIZE_ENGINE=${engine}"
    "-DSIZE_ALGORITHMS=${algorithms}")
  if(program STREQUAL "failed")
    set(${resultVar} "failed" PARENT_SCOPE)
    return()
  endif()

  string(REPLACE " " ";" baselineParts "${baseline}")
  list(GET baselineParts 0 baselineText)
  list(GET baselineParts 1 baselineRam)
  string(REPLACE " " ";" programParts "${program}")
  list(GET programParts 0 programText)
  list(GET programParts 1 programRam)

  math(EXPR costText "${programText} - ${baselineText}")
  math(EXPR costRam "${programRam} - ${baselineRam}")
  set(${resultVar} "${costText} ${costRam}" PARENT_SCOPE)
endfunction()


# The name of the limit variable of one row for one CPU.
function(vhalSizeLimitName resultVar opt engine algorithms cpu)
  string(REPLACE "-" "_" cpuKey "${cpu}")
  set(${resultVar} "vhalSizeLimit_${opt}_${engine}_${algorithms}_${cpuKey}" PARENT_SCOPE)
endfunction()


# Prints the header of one table.
function(vhalSizeHeader)
  message("")
  message("### -${ARGN}")
  message("")
  message("| Engine | Algorithms | M4 text | M4 RAM | M0+ text | M0+ RAM |")
  message("|---|---:|---:|---:|---:|---:|")
endfunction()


set(vhalSizeFailures 0)

foreach(opt IN ITEMS Os O2)
  vhalSizeHeader(${opt})

  foreach(engine IN ITEMS CrcBitwise CrcNibble CrcTable)
    foreach(algorithms IN ITEMS 1 3)
      set(line "| ${engine} | ${algorithms}")
      foreach(cpu IN ITEMS cortex-m4 cortex-m0plus)
        vhalSizeCost(cost "${opt}" "${cpu}" "${engine}" "${algorithms}")
        if(cost STREQUAL "failed")
          math(EXPR vhalSizeFailures "${vhalSizeFailures} + 1")
          string(APPEND line " | failed | failed")
          continue()
        endif()

        string(REPLACE " " ";" costParts "${cost}")
        list(GET costParts 0 costText)
        list(GET costParts 1 costRam)

        vhalSizeLimitName(limitName "${opt}" "${engine}" "${algorithms}" "${cpu}")
        if(NOT DEFINED ${limitName})
          message("NO LIMIT: ${opt} ${engine} ${algorithms} ${cpu}")
          math(EXPR vhalSizeFailures "${vhalSizeFailures} + 1")
        elseif(${costText} GREATER ${${limitName}})
          message("OVER LIMIT: ${opt} ${engine} ${algorithms} ${cpu} is ${costText} bytes of text, the limit is ${${limitName}}")
          math(EXPR vhalSizeFailures "${vhalSizeFailures} + 1")
        endif()

        if(NOT costRam EQUAL 0)
          message("RAM NOT ZERO: ${opt} ${engine} ${algorithms} ${cpu} is ${costRam} bytes")
          math(EXPR vhalSizeFailures "${vhalSizeFailures} + 1")
        endif()

        string(APPEND line " | ${costText} | ${costRam}")
      endforeach()
      message("${line} |")
    endforeach()
  endforeach()
endforeach()

# The bitwise engine at -O3: the compiler's -foptimize-crc must not turn its loop into a table. Its rows are checked alone.
message("")
message("### -O3, bitwise only")
message("")
message("| Engine | Algorithms | M4 text | M4 RAM | M0+ text | M0+ RAM |")
message("|---|---:|---:|---:|---:|---:|")
foreach(algorithms IN ITEMS 1 3)
  set(line "| CrcBitwise | ${algorithms}")
  foreach(cpu IN ITEMS cortex-m4 cortex-m0plus)
    vhalSizeCost(cost O3 "${cpu}" CrcBitwise "${algorithms}")
    if(cost STREQUAL "failed")
      math(EXPR vhalSizeFailures "${vhalSizeFailures} + 1")
      string(APPEND line " | failed | failed")
      continue()
    endif()

    string(REPLACE " " ";" costParts "${cost}")
    list(GET costParts 0 costText)
    list(GET costParts 1 costRam)

    vhalSizeLimitName(limitName O3 CrcBitwise "${algorithms}" "${cpu}")
    if(NOT DEFINED ${limitName})
      message("NO LIMIT: O3 CrcBitwise ${algorithms} ${cpu}")
      math(EXPR vhalSizeFailures "${vhalSizeFailures} + 1")
    elseif(${costText} GREATER ${${limitName}})
      message("OVER LIMIT: O3 CrcBitwise ${algorithms} ${cpu} is ${costText} bytes of text, the limit is ${${limitName}}")
      math(EXPR vhalSizeFailures "${vhalSizeFailures} + 1")
    endif()

    string(APPEND line " | ${costText} | ${costRam}")
  endforeach()
  message("${line} |")
endforeach()

message("")
execute_process(
  COMMAND "${VHAL_ARM_CXX}" --version
  OUTPUT_VARIABLE armVersionOut
  ERROR_QUIET)
string(REPLACE "\r" "" armVersionOut "${armVersionOut}")
string(REGEX MATCH "^[^\n]*" armVersionLine "${armVersionOut}")
message("Toolchain: ${armVersionLine}")

if(vhalSizeFailures GREATER 0)
  message(FATAL_ERROR "${vhalSizeFailures} CRC size check(s) failed")
endif()
