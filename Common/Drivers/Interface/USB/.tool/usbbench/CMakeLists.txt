cmake_minimum_required(VERSION 3.21)

project(usbbench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Libusb.cmake")

find_package(Threads REQUIRED)

add_executable(usbbench
	src/BackendFactory.cpp
	src/BulkLane.cpp
	src/Commands.cpp
	src/DeviceSelection.cpp
	src/LibUsbBackend.cpp
	src/Options.cpp
	src/Summary.cpp
	src/main.cpp
)

# The benchmark protocol header is shared with the firmware and lives one folder up.
target_include_directories(usbbench PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../common"
)

target_compile_options(usbbench PRIVATE
	-Wall
	-Wextra
	-Wpedantic
)

target_link_libraries(usbbench PRIVATE ${USBBENCH_LIBUSB_TARGET} Threads::Threads)

if(WIN32)
	target_sources(usbbench PRIVATE src/WinUsbBackend.cpp)
	target_link_libraries(usbbench PRIVATE setupapi ole32 winusb)
endif()

# Offline test of the lane scheduling and pattern check. It needs no device and no libusb.
enable_testing()

add_executable(usbbenchBulkLaneTest
	test/BulkLaneTest.cpp
	src/BulkLane.cpp
)

target_include_directories(usbbenchBulkLaneTest PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../common"
)

target_compile_options(usbbenchBulkLaneTest PRIVATE
	-Wall
	-Wextra
	-Wpedantic
)

add_test(NAME usbbenchBulkLane COMMAND usbbenchBulkLaneTest)

# Offline test of the command line rules for --serial, --all and list. It needs no device and no libusb.
add_executable(usbbenchOptionsTest
	test/OptionsTest.cpp
	src/Options.cpp
)

target_include_directories(usbbenchOptionsTest PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../common"
)

target_compile_options(usbbenchOptionsTest PRIVATE
	-Wall
	-Wextra
	-Wpedantic
)

add_test(NAME usbbenchOptions COMMAND usbbenchOptionsTest)

# Offline test of the verdict and the --all totals. It needs no device and no libusb.
add_executable(usbbenchSummaryTest
	test/SummaryTest.cpp
	src/Summary.cpp
)

target_include_directories(usbbenchSummaryTest PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(usbbenchSummaryTest PRIVATE
	-Wall
	-Wextra
	-Wpedantic
)

add_test(NAME usbbenchSummary COMMAND usbbenchSummaryTest)

# Offline test of which device a command picks, with and without --serial and --all. It needs no device and no libusb.
add_executable(usbbenchDeviceSelectionTest
	test/DeviceSelectionTest.cpp
	src/DeviceSelection.cpp
)

target_include_directories(usbbenchDeviceSelectionTest PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../common"
)

target_compile_options(usbbenchDeviceSelectionTest PRIVATE
	-Wall
	-Wextra
	-Wpedantic
)

add_test(NAME usbbenchDeviceSelection COMMAND usbbenchDeviceSelectionTest)

# Offline test of the start gate of a --all run: release when all arrive, cancel, and a late arrival. It needs no device.
add_executable(usbbenchStartGateTest
	test/StartGateTest.cpp
)

target_include_directories(usbbenchStartGateTest PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(usbbenchStartGateTest PRIVATE
	-Wall
	-Wextra
	-Wpedantic
)

target_link_libraries(usbbenchStartGateTest PRIVATE Threads::Threads)

add_test(NAME usbbenchStartGate COMMAND usbbenchStartGateTest)
