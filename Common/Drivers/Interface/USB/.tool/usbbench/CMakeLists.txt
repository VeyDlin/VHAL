cmake_minimum_required(VERSION 3.21)

project(usbbench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Libusb.cmake")

add_executable(usbbench
	src/BackendFactory.cpp
	src/BulkLane.cpp
	src/Commands.cpp
	src/LibUsbBackend.cpp
	src/Options.cpp
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

target_link_libraries(usbbench PRIVATE ${USBBENCH_LIBUSB_TARGET})

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
