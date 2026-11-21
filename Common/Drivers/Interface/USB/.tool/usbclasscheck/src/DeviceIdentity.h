#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A demo board on the bus. The serial is empty when Windows built the device instance itself, which it does for a board
// without a serial string.
struct BoardInfo {
	std::uint16_t productId = 0;
	std::string serial;
};

// The parts of a device instance ID, such as USB\VID_1209&PID_0002&MI_04\7&1A2B&0&0004. The bus and the IDs are read in
// upper case; the segment keeps the case it had.
struct InstanceParts {
	std::string bus;
	std::uint16_t vendorId = 0;
	std::uint16_t productId = 0;
	// The number from the MI_ part, or -1 when the instance is the whole device and not one of its interfaces.
	int interfaceNumber = -1;
	std::string segment;
};

enum class BoardSelection : std::uint8_t {
	Chosen,
	NoBoard,
	SerialNotFound,
	SerialOfOtherDemo,
	Several
};

// The demo name of a product ID, such as USBComposite. Empty for an ID that is not one of the demos.
std::string DemoNameOf(std::uint16_t productId);

bool IsDemoProduct(std::uint16_t productId);

// One line for the list: the PID, the demo name and the serial number, or "(no serial number)".
std::string BoardLine(const BoardInfo& board);

// The device instance ID of a device interface path. "\\?\usb#vid_1209&pid_0002#SERIAL#{guid}" becomes
// "usb\vid_1209&pid_0002\SERIAL". Empty when the path has no bus and no ID part.
std::string InstanceFromInterfacePath(const std::string& path);

bool ParseInstance(const std::string& instance, InstanceParts& parts);

// The USB device node of a board: the whole device, not one of its interfaces, with a segment for its instance.
bool IsBoardNode(const InstanceParts& parts);

// The serial number of a board node. Windows uses the serial string as the instance segment; a segment with an '&' was
// built by Windows, so the board has no serial string and the result is empty.
std::string SerialOfBoardNode(const InstanceParts& parts);

// A wanted serial matches a found one ignoring case. An empty wanted serial matches any board.
bool SerialMatches(const std::string& wanted, const std::string& found);

// True when the USB device node above a storage disk is this board's: the same vendor, the storage PID and the board's serial. A board
// with no serial is never matched, because another board of the same IDs could not be told from it. reason says why not when false.
bool IsBoardStorageNode(const std::string& instance, const BoardInfo& board, std::string& reason);

// True when the instance is the USB device node of this board: the vendor, the board's own product ID and its serial. A board with no
// serial is never matched. reason says why not when false. The audio check uses it to find the board's USB device node.
bool IsNodeOfBoard(const std::string& instance, const BoardInfo& board, std::string& reason);

// True when a container ID names a device group: it is not empty and not the null container ID, which Windows gives a device it cannot
// group. The text is the GUID, with or without braces, ignoring case.
bool HasContainerId(const std::string& containerId);

// True when the endpoint's container ID is the board's. Windows gives every device of one physical device the same container ID, so the
// board's USB device node and its audio endpoints share one. Neither may be empty or null, and the text is compared ignoring case.
bool ContainerIdMatches(const std::string& boardContainer, const std::string& endpointContainer);

// The indexes of the endpoints whose container ID is the board's, in the order they are listed. Empty when the board has no usable
// container ID.
std::vector<std::size_t> MatchingEndpoints(const std::string& boardContainer, const std::vector<std::string>& endpointContainers);

// Picks the board a command acts on. With a serial, the board that reports it must be a board of the wanted demo.
// Without one, the demo must have exactly one board attached. index is set when the result is Chosen.
BoardSelection SelectBoard(
	const std::vector<BoardInfo>& boards,
	std::uint16_t productId,
	const std::string& serial,
	std::size_t& index);
