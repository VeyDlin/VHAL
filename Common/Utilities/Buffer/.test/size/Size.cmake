# Measures what the ring buffer and the packet queue cost on a Cortex-M0+ and a Cortex-M4, in bytes of text and of RAM, at -Os and -O2.
# Each cost is a program minus the baseline of the same optimisation and CPU, which makes the same call with no ring. RAM is data plus
# bss: the storage of a ring is static, so it shows as RAM. The critical section of a port is stubbed in the programs, so the locked rows
# show the calls to the section, not the port's code for it. The VUB ring form (volatile indexes with std::atomic_signal_fence) is
# measured for the record.
#
# The script prints a markdown table, then the toolchain. It prints the VHAL_SKIP marker and passes when the ARM compiler is absent or
# older than GCC 14. It fails when a program does not build.
cmake_minimum_required(VERSION 3.25)

set(vhalSizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(bufferArmVersion)
if(bufferArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the ring size table was not measured")
  return()
endif()

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(vhalSizeIncludes
  -I "${vhalSizeDir}/shim"
  -I "${vhalSizeDir}"
  -I "${VHAL_ROOT}/Common"
  -I "${VHAL_ROOT}/Periphery")

set(bufferSizeCpus cortex-m0plus cortex-m4)
set(bufferSizeOptimisations Os O2)


# One row: the text and the RAM of a program with the given source and defines, minus the baseline of its optimisation and CPU. Sets
# resultVar to "ok" when the row is printed, or to "failed" when a program does not build.
function(bufferSizeRow resultVar opt cpu baseline label name source)
  vhalSizeProgram(program "${opt}" "${cpu}" "${name}-${opt}-${cpu}" "${source}" ${ARGN})
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
  message("| -${opt} | ${cpu} | ${label} | ${costText} | ${costRam} |")
  set(${resultVar} "ok" PARENT_SCOPE)
endfunction()


set(bufferSizeFailures 0)

message("")
message("| Optimisation | CPU | Program | Text | RAM |")
message("|---|---|---|---:|---:|")

foreach(opt IN LISTS bufferSizeOptimisations)
  foreach(cpu IN LISTS bufferSizeCpus)
    vhalSizeProgram(baseline "${opt}" "${cpu}" "baseline-${opt}-${cpu}" "${vhalSizeDir}/SizeBaseline.cpp")
    if(baseline STREQUAL "failed")
      math(EXPR bufferSizeFailures "${bufferSizeFailures} + 1")
      continue()
    endif()

    set(rows
      "ringLocked|RingBuffer<uint8, 64>, locked|${vhalSizeDir}/SizeRing.cpp|-DSIZE_ACCESS=locked"
      "ringSingleWriter|RingBuffer<uint8, 64>, singleWriter|${vhalSizeDir}/SizeRing.cpp|-DSIZE_ACCESS=singleWriter"
      "ringCallerLocked|RingBuffer<uint8, 64>, callerLocked|${vhalSizeDir}/SizeRing.cpp|-DSIZE_ACCESS=callerLocked"
      "packetLocked|PacketQueue<64, 8>, locked|${vhalSizeDir}/SizePacket.cpp|-DSIZE_ACCESS=locked"
      "packetSingleWriter|PacketQueue<64, 8>, singleWriter|${vhalSizeDir}/SizePacket.cpp|-DSIZE_ACCESS=singleWriter"
      "packetCallerLocked|PacketQueue<64, 8>, callerLocked|${vhalSizeDir}/SizePacket.cpp|-DSIZE_ACCESS=callerLocked"
      "vubRing|VUB ring form, 64 slots|${vhalSizeDir}/SizeVub.cpp|-DSIZE_VUB=1")

    foreach(row IN LISTS rows)
      string(REPLACE "|" ";" rowParts "${row}")
      list(GET rowParts 0 rowName)
      list(GET rowParts 1 rowLabel)
      list(GET rowParts 2 rowSource)
      list(GET rowParts 3 rowDefine)

      bufferSizeRow(rowResult "${opt}" "${cpu}" "${baseline}" "${rowLabel}" "${rowName}" "${rowSource}" "${rowDefine}")

      if(rowResult STREQUAL "failed")
        math(EXPR bufferSizeFailures "${bufferSizeFailures} + 1")
      endif()
    endforeach()
  endforeach()
endforeach()

message("")
execute_process(
  COMMAND "${VHAL_ARM_CXX}" --version
  OUTPUT_VARIABLE armVersionOut
  ERROR_QUIET)
string(REPLACE "\r" "" armVersionOut "${armVersionOut}")
string(REGEX MATCH "^[^\n]*" armVersionLine "${armVersionOut}")
message("${armVersionLine}")

if(bufferSizeFailures GREATER 0)
  message(FATAL_ERROR "${bufferSizeFailures} ring size program(s) failed to build")
endif()
