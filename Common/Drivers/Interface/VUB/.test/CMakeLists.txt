# Host tests of the VUB core, the builder error snippets, the header check and the size table. The include roots of every test are the
# ones of the core's test script, in its order: Common, this folder, the adapter shim, Periphery. The two tests that run the real USB
# functions take the USB folder's shim and folder first, then the USB common folder and the demo application folder.
#
# The PC links differ per system. Windows links the WinUSB, HID and socket libraries; Linux links libusb-1.0 for the bulk and wiki
# tests and pthread for the concurrency tests. Other systems link no extra library.

set(vubIncludes
  "${VHAL_ROOT}/Common"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${VHAL_ROOT}/Periphery/Adapter/.test/shim"
  "${VHAL_ROOT}/Periphery")

set(vubUsbFolder "${VHAL_ROOT}/Common/Drivers/Interface/USB")
set(vubUsbIncludes
  "${vubUsbFolder}/.test/shim"
  "${vubUsbFolder}/.test"
  "${vubUsbFolder}/.tool/common"
  "${VHAL_ROOT}/.demo/STM32G4/Application"
  ${vubIncludes})

set(vubUsbFound TRUE)

if(WIN32)
  set(vubBulkLibs winusb setupapi cfgmgr32)
  set(vubHidLibs hid setupapi)
  set(vubTcpLibs ws2_32)
  set(vubWikiLibs ws2_32 setupapi cfgmgr32 winusb hid)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_library(vubUsbLibrary NAMES usb-1.0)
  find_path(vubUsbInclude NAMES libusb-1.0/libusb.h)
  if(NOT vubUsbLibrary OR NOT vubUsbInclude)
    set(vubUsbFound FALSE)
  endif()

  set(vubBulkLibs usb-1.0)
  set(vubWikiLibs usb-1.0)
  set(vubThreadLibs pthread)
endif()

# A test that needs a library the system lacks is reported as skipped, with the marker the other skips use.
function(vubSkipHostTest source)
  get_filename_component(stem "${source}" NAME_WE)
  set(testName "${VHAL_MODULE_LABEL}.${stem}")

  add_test(
    NAME "${testName}"
    COMMAND "${CMAKE_COMMAND}" -E echo "VHAL_SKIP: libusb-1.0 (the library and libusb-1.0/libusb.h) is not installed")
  set_tests_properties("${testName}" PROPERTIES LABELS "${VHAL_MODULE_LABEL};host" SKIP_REGULAR_EXPRESSION "VHAL_SKIP:")
endfunction()

set(vubPlainTests
  TestVubBuilder.cpp
  TestVubBulkLink.cpp
  TestVubCdcLink.cpp
  TestVubCobs.cpp
  TestVubConcepts.cpp
  TestVubConfigured.cpp
  TestVubCrcChain.cpp
  TestVubCrcWiki.cpp
  TestVubCrcWire.cpp
  TestVubDeliverUp.cpp
  TestVubFraming.cpp
  TestVubFramingSize.cpp
  TestVubFramingStacks.cpp
  TestVubFramingWiki.cpp
  TestVubGap.cpp
  TestVubHidLink.cpp
  TestVubHidReport.cpp
  TestVubKeepAlive.cpp
  TestVubLengthPrefix.cpp
  TestVubLinkLoss.cpp
  TestVubLinkState.cpp
  TestVubLoopback.cpp
  TestVubMemory.cpp
  TestVubOutage.cpp
  TestVubPcRelease.cpp
  TestVubPcSerial.cpp
  TestVubPeerDown.cpp
  TestVubReceiver.cpp
  TestVubReliableBoot.cpp
  TestVubReliableClock.cpp
  TestVubReliableDelivery.cpp
  TestVubReliableFaults.cpp
  TestVubReliableInOrder.cpp
  TestVubReliableLink.cpp
  TestVubReliableRejoin.cpp
  TestVubReliableReplay.cpp
  TestVubReliableRestart.cpp
  TestVubReliableSession.cpp
  TestVubReliableSyncReset.cpp
  TestVubReliableWiki.cpp
  TestVubReliableWire.cpp
  TestVubRequestsBody.cpp
  TestVubRequestsCancel.cpp
  TestVubRequestsCommands.cpp
  TestVubRequestsEvents.cpp
  TestVubRequestsHandlers.cpp
  TestVubRequestsHealth.cpp
  TestVubRequestsLayerWiki.cpp
  TestVubRequestsMix.cpp
  TestVubRequestsModel.cpp
  TestVubRequestsNoBodyCall.cpp
  TestVubRequestsPath.cpp
  TestVubRequestsPing.cpp
  TestVubRequestsRequester.cpp
  TestVubRequestsResponder.cpp
  TestVubRequestsWiki.cpp
  TestVubRequestsWire.cpp
  TestVubStack.cpp
  TestVubStackBulk.cpp
  TestVubStackCounters.cpp
  TestVubStackLayers.cpp
  TestVubStackMinimal.cpp
  TestVubStackOwned.cpp
  TestVubStackStarvation.cpp
  TestVubSteps.cpp
  TestVubStorage.cpp
  TestVubTestKitWiki.cpp
  TestVubTransform.cpp
  TestVubUartLink.cpp
  TestVubWikiExamples.cpp)

foreach(vubTest IN LISTS vubPlainTests)
  vhal_test(${vubTest} INCLUDES ${vubIncludes})
endforeach()

vhal_test(TestVubLinkUsb.cpp INCLUDES ${vubUsbIncludes})
vhal_test(TestVubLinkWiki.cpp INCLUDES ${vubUsbIncludes})
if(vubUsbFound)
  vhal_test(TestVubPcBulk.cpp INCLUDES ${vubIncludes} LIBS ${vubBulkLibs})
  vhal_test(TestVubPcWiki.cpp INCLUDES ${vubIncludes} LIBS ${vubWikiLibs})
else()
  vubSkipHostTest(TestVubPcBulk.cpp)
  vubSkipHostTest(TestVubPcWiki.cpp)
endif()

vhal_test(TestVubPcHid.cpp INCLUDES ${vubIncludes} LIBS ${vubHidLibs})
vhal_test(TestVubPcHidRelease.cpp INCLUDES ${vubIncludes} LIBS ${vubHidLibs})
vhal_test(TestVubPcTcp.cpp INCLUDES ${vubIncludes} LIBS ${vubTcpLibs})
vhal_test(TestVubPcTcpReliable.cpp INCLUDES ${vubIncludes} LIBS ${vubTcpLibs})
vhal_test(TestVubRequestsConcurrent.cpp INCLUDES ${vubIncludes} LIBS ${vubThreadLibs})
vhal_test(TestVubRequestsQueue.cpp INCLUDES ${vubIncludes} LIBS ${vubThreadLibs})
vhal_test(TestVubStackConcurrent.cpp INCLUDES ${vubIncludes} LIBS ${vubThreadLibs})

vhal_declared(Test*.cpp
  ${vubPlainTests}
  TestVubLinkUsb.cpp
  TestVubLinkWiki.cpp
  TestVubPcBulk.cpp
  TestVubPcHid.cpp
  TestVubPcHidRelease.cpp
  TestVubPcTcp.cpp
  TestVubPcTcpReliable.cpp
  TestVubPcWiki.cpp
  TestVubRequestsConcurrent.cpp
  TestVubRequestsQueue.cpp
  TestVubStackConcurrent.cpp)

vhal_fail(BuilderErrors INCLUDES "${VHAL_ROOT}/Common" "${VHAL_ROOT}/Periphery")

vhal_check(HeaderCheck.cmake)

vhal_size(size/Size.cmake)

vhal_tool_test(../.tool/board)
