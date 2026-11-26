cmake_minimum_required(VERSION 3.21)

project(usbclasscheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The shared identity header lives one folder up, with the PC tools' common code.
function(usbclasscheck_configure target)
	target_include_directories(${target} PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}/../common"
	)

	target_compile_options(${target} PRIVATE
		-Wall
		-Wextra
		-Wpedantic
	)
endfunction()

enable_testing()

# The pure logic has no Win32 calls, so its tests run without a board. Each test is one executable, as in usbbench.
function(usbclasscheck_test name source)
	add_executable(${name} ${source} ${ARGN})
	usbclasscheck_configure(${name})
	add_test(NAME ${name} COMMAND ${name})
endfunction()

usbclasscheck_test(usbclasscheckOptionsTest test/OptionsTest.cpp src/Options.cpp)
usbclasscheck_test(usbclasscheckReportTest test/ReportTest.cpp src/Report.cpp)
usbclasscheck_test(usbclasscheckDeviceIdentityTest test/DeviceIdentityTest.cpp src/DeviceIdentity.cpp)
usbclasscheck_test(usbclasscheckStatusAnswerTest test/StatusAnswerTest.cpp src/StatusAnswer.cpp)
usbclasscheck_test(usbclasscheckRawInputLogicTest test/RawInputLogicTest.cpp src/RawInputLogic.cpp)
usbclasscheck_test(usbclasscheckCrc32Test test/Crc32Test.cpp src/Crc32.cpp)
usbclasscheck_test(usbclasscheckCheckDataTest test/CheckDataTest.cpp src/CheckData.cpp)
usbclasscheck_test(usbclasscheckNetworkLogicTest test/NetworkLogicTest.cpp src/NetworkLogic.cpp src/CheckData.cpp src/Report.cpp)
usbclasscheck_test(usbclasscheckAudioLogicTest test/AudioLogicTest.cpp src/AudioLogic.cpp)
usbclasscheck_test(usbclasscheckAudioFormatTest test/AudioFormatTest.cpp src/AudioFormat.cpp)
usbclasscheck_test(usbclasscheckTransferLogicTest test/TransferLogicTest.cpp src/TransferLogic.cpp)
usbclasscheck_test(usbclasscheckProcessPollTest test/ProcessPollTest.cpp src/ProcessPoll.cpp)

if(WIN32)
	add_executable(usbclasscheck
		src/AudioEndpoints.cpp
		src/AudioFormat.cpp
		src/AudioLogic.cpp
		src/AudioStreams.cpp
		src/CheckAudio.cpp
		src/CheckComposite.cpp
		src/CheckData.cpp
		src/CheckDfu.cpp
		src/CheckNetwork.cpp
		src/CheckStorage.cpp
		src/Commands.cpp
		src/ConsoleCapture.cpp
		src/Crc32.cpp
		src/DeviceIdentity.cpp
		src/DeviceTree.cpp
		src/IcmpEcho.cpp
		src/NetworkAdapters.cpp
		src/NetworkLogic.cpp
		src/Options.cpp
		src/ProcessPoll.cpp
		src/ProcessRun.cpp
		src/RawInputListener.cpp
		src/RawInputLogic.cpp
		src/Report.cpp
		src/SerialPort.cpp
		src/StatusAnswer.cpp
		src/TransferLogic.cpp
		src/VolumeAccess.cpp
		src/WinUsbVendor.cpp
		src/main.cpp
	)

	usbclasscheck_configure(usbclasscheck)
	# The IP Helper calls (neighbour table, adapter table, ICMP) need Windows Vista or later in the SDK headers, and the tool
	# runs on Windows 10 and 11 only.
	target_compile_definitions(usbclasscheck PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
	target_link_libraries(usbclasscheck PRIVATE setupapi cfgmgr32 winusb ole32 user32 advapi32 uuid iphlpapi ws2_32)
endif()
