# Measures what the VUB configurations cost on a Cortex-M4 and a Cortex-M0+ target, in bytes. Each configuration is a program made of one
# ConfigX.cpp (or ConfigCompact.cpp for the CompactStreamingProtocol comparison), SizeMain.cpp and the baseline of its link kind
# (BaselineStream.cpp or BaselinePacket.cpp). The baseline makes the same calls with no VUB, so the cost of a configuration is the
# program minus its baseline. Each program is compiled with the ARM compiler at -Os with -std=c++23, -ffunction-sections,
# -fdata-sections, -fno-exceptions and -fno-rtti, and linked with --gc-sections and the nano and nosys specs. The size tool reads text,
# data and bss. RAM is data plus bss.
#
# The script prints a markdown table of the costs, then the toolchain and the baseline sizes. It prints the VHAL_SKIP marker and
# passes when the ARM compiler is absent or older than GCC 14. It fails when a program does not build.
cmake_minimum_required(VERSION 3.25)

set(vhalSizeDir "${CMAKE_CURRENT_LIST_DIR}")
include("${VHAL_ROOT}/.test/VhalSize.cmake")

vhalSizeToolchainVersion(vubArmVersion)
if(vubArmVersion STREQUAL "")
  message("VHAL_SKIP: ${VHAL_ARM_CXX} is absent or older than GCC 14: the VUB size table was not measured")
  return()
endif()

file(REMOVE_RECURSE "${VHAL_WORK_DIR}")
file(MAKE_DIRECTORY "${VHAL_WORK_DIR}")

set(vhalSizeIncludes
  -I "${vhalSizeDir}/shim"
  -I "${vhalSizeDir}"
  -I "${VHAL_ROOT}/Common"
  -I "${VHAL_ROOT}/Periphery")

set(vubSizeCpus cortex-m4 cortex-m0plus)
set(vubSizeBaselines BaselinePacket.cpp BaselineStream.cpp)

# The rows: the label, the baseline source, its flags, the configuration source, and its flags, separated by bars. The flags are
# the defines of the row. A row whose configuration is the requests layer (ConfigRequests.cpp) takes the same chain with SIZE_ROLE=0
# as its baseline: the cost is the layer on that chain.
set(vubSizeRows
  "(a) packet link only|BaselinePacket.cpp||ConfigA.cpp|"
  "(b) stream link, COBS|BaselineStream.cpp||ConfigB.cpp|"
  "(c) (b) + CRC-16/MODBUS, default engine (bitwise)|BaselineStream.cpp||ConfigC.cpp|"
  "(b) with CRC-16, nothing else|BaselineStream.cpp||ConfigMinimal.cpp|"
  "(b) with CRC-16, SendQueue(4) and CountDrops()|BaselineStream.cpp||ConfigMinimal.cpp|-DSIZE_OPTIONS=.SendQueue(4).CountDrops()"
  "(d) (b) + CRC-16/MODBUS, table engine named|BaselineStream.cpp||ConfigD.cpp|"
  "(e) stream link, length prefix 1, CRC-8 bitwise|BaselineStream.cpp||ConfigE.cpp|"
  "(f) stream link, gap framing, CRC-16 table|BaselineStream.cpp||ConfigF.cpp|"
  "(g) (c) + VUB::Reliable (Retries 3, Window 4)|BaselineStream.cpp||ConfigG.cpp|"
  "(h) (g) with the in-order option|BaselineStream.cpp||ConfigH.cpp|"
  "(i) COBS, CRC-8, VUB::ReliableOf window 1, Retries 3|BaselineStream.cpp||ConfigI.cpp|"
  "breakdown of (i): the same without VUB::ReliableOf|BaselineStream.cpp||ConfigIPlain.cpp|"
  "CompactStreamingProtocol<20, 4> (bootloader use)|BaselinePacket.cpp||ConfigCompact.cpp|"
  "(j) stream link, COBS, CRC-16/MODBUS (default engine), VUB::KeepAlive (default times)|BaselineStream.cpp||ConfigJ.cpp|"
  "(k) (g) + VUB::KeepAlive (default times)|BaselineStream.cpp||ConfigK.cpp|"
  "requests responder only, CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=1"
  "requests requester only (one Send site), CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=2"
  "requests both, CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=3"
  "requests requester only (four Send sites), CRC-16/MODBUS over COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=1 -DSIZE_ROLE=2 -DSIZE_SENDS=4"
  "requests responder only, COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=1"
  "requests requester only (one Send site), COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=2"
  "requests both, COBS over stream link|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=2 -DSIZE_ROLE=3"
  "requests responder only, ReliableOf<40, 4>|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=1"
  "requests requester only (one Send site), ReliableOf<40, 4>|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=2"
  "requests both, ReliableOf<40, 4>|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=0|ConfigRequests.cpp|-DSIZE_BASE=3 -DSIZE_ROLE=3"
  "concurrent requests: requester, plain (Open(2)), COBS over stream link|ConfigConcurrent.cpp|-DSIZE_ROLE=0 -DSIZE_CONC=0|ConfigConcurrent.cpp|-DSIZE_ROLE=2 -DSIZE_CONC=0"
  "concurrent requests: requester, Pending(4), Awaitable(), Concurrent()|ConfigConcurrent.cpp|-DSIZE_ROLE=0 -DSIZE_CONC=0|ConfigConcurrent.cpp|-DSIZE_ROLE=2 -DSIZE_CONC=1 -DSIZE_AWAIT=0"
  "concurrent requests: responder, plain (Tickets(1), Refusals(2))|ConfigConcurrent.cpp|-DSIZE_ROLE=0 -DSIZE_CONC=0|ConfigConcurrent.cpp|-DSIZE_ROLE=1 -DSIZE_CONC=0"
  "concurrent requests: responder, Pending(4), Awaitable(), Concurrent()|ConfigConcurrent.cpp|-DSIZE_ROLE=0 -DSIZE_CONC=0|ConfigConcurrent.cpp|-DSIZE_ROLE=1 -DSIZE_CONC=1"
  "concurrent requests: Await of one request, as the difference to the requester without the call|ConfigConcurrent.cpp|-DSIZE_ROLE=2 -DSIZE_CONC=1 -DSIZE_AWAIT=0|ConfigConcurrent.cpp|-DSIZE_ROLE=2 -DSIZE_CONC=1 -DSIZE_AWAIT=1"
  "health asks, next to a served command and ping, with the counters, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1 -DSIZE_PING=1|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1 -DSIZE_PING=1 -DSIZE_HEALTH=1"
  "health answers, next to a served command and ping, with the counters, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1 -DSIZE_PING=1|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1 -DSIZE_PING=1 -DSIZE_HEALTH=2"
  "health both, next to a served command and ping, with the counters, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1 -DSIZE_PING=1|ConfigHealth.cpp|-DSIZE_LAYER=3 -DSIZE_COUNTED=1 -DSIZE_PING=1 -DSIZE_HEALTH=3"
  "health asks in a program with no requester (send path), served command with the counters, ReliableOf<40, 4>|ConfigHealth.cpp|-DSIZE_LAYER=1 -DSIZE_COUNTED=1|ConfigHealth.cpp|-DSIZE_LAYER=1 -DSIZE_COUNTED=1 -DSIZE_HEALTH=1")

# Sets vubSizeText and vubSizeRam (text, and data plus bss) of a linked image, and vubSizeData and vubSizeBss, read with the size tool.
# A failure of the size tool stops the script, because a missing reading would make the table wrong.
function(vubSizeSections image)
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

  set(vubSizeText "${text}" PARENT_SCOPE)
  set(vubSizeData "${data}" PARENT_SCOPE)
  set(vubSizeBss "${bss}" PARENT_SCOPE)
endfunction()


# Measures the program of one source and defines for a CPU. Sets vubSizeMeasured to "text ram" of the linked image, or to "failed"
# when a step fails, and vubSizeImage to the path of the image. Each program is built once: its result is kept in a variable named
# after its key, which is set in the calling scope.
macro(vubSizeMeasure cpu source flags)
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" vubSizeKey "${cpu}_${source}_${flags}")
  if(NOT DEFINED vubSizeValue_${vubSizeKey})
    separate_arguments(vubSizeDefines UNIX_COMMAND "${flags}")
    vhalSizeProgram(vubSizeResult Os "${cpu}" "${vubSizeKey}" "${vhalSizeDir}/${source}" ${vubSizeDefines})
    set(vubSizeValue_${vubSizeKey} "${vubSizeResult}")
    set(vubSizeKeyOf_${vubSizeKey} "${VHAL_WORK_DIR}/${vubSizeKey}.elf")
  endif()
  set(vubSizeMeasured "${vubSizeValue_${vubSizeKey}}")
  set(vubSizeImage "${vubSizeKeyOf_${vubSizeKey}}")
endmacro()


set(vubSizeFailures 0)

message("")
message("| Configuration | M4 text | M4 RAM | M0+ text | M0+ RAM |")
message("|---|---:|---:|---:|---:|")

foreach(vubSizeRow IN LISTS vubSizeRows)
  string(REPLACE "|" ";" vubSizeParts "${vubSizeRow}")
  list(GET vubSizeParts 0 rowLabel)
  list(GET vubSizeParts 1 baseSource)
  list(GET vubSizeParts 2 baseFlags)
  list(GET vubSizeParts 3 configSource)
  list(GET vubSizeParts 4 configFlags)

  set(rowLine "| ${rowLabel}")
  foreach(cpu IN LISTS vubSizeCpus)
    vubSizeMeasure("${cpu}" "${baseSource}" "${baseFlags}")
    set(baseMeasured "${vubSizeMeasured}")
    vubSizeMeasure("${cpu}" "${configSource}" "${configFlags}")
    set(configMeasured "${vubSizeMeasured}")

    if(baseMeasured STREQUAL "failed" OR configMeasured STREQUAL "failed")
      math(EXPR vubSizeFailures "${vubSizeFailures} + 1")
      string(APPEND rowLine " | failed | failed")
      continue()
    endif()

    string(REPLACE " " ";" baseParts "${baseMeasured}")
    list(GET baseParts 0 baseText)
    list(GET baseParts 1 baseRam)
    string(REPLACE " " ";" configParts "${configMeasured}")
    list(GET configParts 0 configText)
    list(GET configParts 1 configRam)

    math(EXPR costText "${configText} - ${baseText}")
    math(EXPR costRam "${configRam} - ${baseRam}")
    string(APPEND rowLine " | ${costText} | ${costRam}")
  endforeach()

  message("${rowLine} |")
endforeach()

message("")
execute_process(
  COMMAND "${VHAL_ARM_CXX}" --version
  OUTPUT_VARIABLE toolchainOut
  ERROR_QUIET)
string(REPLACE "\r" "" toolchainOut "${toolchainOut}")
string(REGEX MATCH "^[^\n]*" toolchainLine "${toolchainOut}")
message("Toolchain: ${toolchainLine}")

foreach(baseSource IN LISTS vubSizeBaselines)
  foreach(cpu IN LISTS vubSizeCpus)
    vubSizeMeasure("${cpu}" "${baseSource}" "")
    if(vubSizeMeasured STREQUAL "failed")
      continue()
    endif()
    vubSizeSections("${vubSizeImage}")
    message("Baseline ${baseSource} on ${cpu}: text ${vubSizeText}, data ${vubSizeData}, bss ${vubSizeBss}")
  endforeach()
endforeach()

if(vubSizeFailures GREATER 0)
  message(FATAL_ERROR "${vubSizeFailures} row(s) did not build")
endif()
