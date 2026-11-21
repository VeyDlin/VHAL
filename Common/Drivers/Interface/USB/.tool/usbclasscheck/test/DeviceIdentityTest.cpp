#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "DemoProducts.h"
#include "DeviceIdentity.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


void CheckDemoNames() {
	Expect(DemoNameOf(DemoProducts::BenchProductId) == "USBBench", "PID 0001 is USBBench");
	Expect(DemoNameOf(DemoProducts::CompositeProductId) == "USBComposite", "PID 0002 is USBComposite");
	Expect(DemoNameOf(DemoProducts::StorageProductId) == "USBStorage", "PID 0003 is USBStorage");
	Expect(DemoNameOf(DemoProducts::DfuProductId) == "USBDfu", "PID 0004 is USBDfu");
	Expect(DemoNameOf(DemoProducts::NetworkProductId) == "USBNetwork", "PID 0005 is USBNetwork");
	Expect(DemoNameOf(0x0009).empty(), "another PID has no demo name");
	Expect(IsDemoProduct(DemoProducts::DfuProductId) && !IsDemoProduct(0x0009), "IsDemoProduct follows the names");
}


void CheckInterfacePaths() {
	Expect(
		InstanceFromInterfacePath("\\\\?\\usb#vid_1209&pid_0002#0A1B2C#{a5dcbf10-6530-11d2-901f-00c04fb951ed}") == "usb\\vid_1209&pid_0002\\0A1B2C",
		"a USB device path becomes its instance, with the serial kept");
	Expect(
		InstanceFromInterfacePath("\\\\?\\HID#VID_1209&PID_0002&MI_00#7&1A&0&0000#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}")
			== "HID\\VID_1209&PID_0002&MI_00\\7&1A&0&0000",
		"a HID path drops the interface GUID");
	Expect(InstanceFromInterfacePath("garbage").empty(), "a path with no ID part gives no instance");
}


void CheckParseInstance() {
	InstanceParts parts;
	Expect(ParseInstance("USB\\VID_1209&PID_0002&MI_04\\7&1A2B&0&0004", parts), "a function instance parses");
	Expect(parts.bus == "USB", "the bus is read in upper case");
	Expect(parts.vendorId == 0x1209, "the vendor ID is read");
	Expect(parts.productId == 0x0002, "the product ID is read");
	Expect(parts.interfaceNumber == 4, "the MI_ number is read");
	Expect(parts.segment == "7&1A2B&0&0004", "the segment keeps its case and text");
	Expect(!IsBoardNode(parts), "a function is not a board node");

	InstanceParts board;
	Expect(ParseInstance("usb\\vid_1209&pid_0002\\0A1B2C3D", board), "a device node parses in any case");
	Expect(board.interfaceNumber == -1, "a device node has no interface number");
	Expect(IsBoardNode(board), "a device node is a board node");
	Expect(SerialOfBoardNode(board) == "0A1B2C3D", "the serial is the segment of a board node");

	InstanceParts generated;
	Expect(ParseInstance("USB\\VID_1209&PID_0002\\6&2A3B4C5&0&1", generated), "an instance built by Windows parses");
	Expect(SerialOfBoardNode(generated).empty(), "a segment with an ampersand has no serial");

	InstanceParts other;
	Expect(ParseInstance("USB\\VID_046D&PID_C52B\\1", other), "a foreign vendor parses");
	Expect(other.vendorId == 0x046D, "the foreign vendor is read as it is, the caller checks the vendor");
	Expect(!ParseInstance("USB\\VID_1209\\1", other), "an instance without PID is refused");
	Expect(!ParseInstance("USB\\VID_1209&PID_0002&XX_01\\1", other), "an instance with a bad third part is refused");
	Expect(!ParseInstance("USB\\VID_120G&PID_0002\\1", other), "a non-hex vendor is refused");
	Expect(!ParseInstance("USB", other), "text without a separator is refused");
}


void CheckSerialMatching() {
	Expect(SerialMatches("", "anything"), "an empty wanted serial matches any board");
	Expect(SerialMatches("0a1b", "0A1B"), "serials match ignoring case");
	Expect(!SerialMatches("0A1B", "0A1C"), "different serials do not match");
	Expect(!SerialMatches("0A1B", ""), "a board with no serial does not match a wanted one");
}


void CheckSelection() {
	std::vector<BoardInfo> one;
	BoardInfo composite;
	composite.productId = DemoProducts::CompositeProductId;
	composite.serial = "AA11";
	one.push_back(composite);

	std::size_t index = 9;
	Expect(
		SelectBoard(one, DemoProducts::CompositeProductId, "", index) == BoardSelection::Chosen && index == 0,
		"one composite is chosen without a serial");
	Expect(
		SelectBoard(one, DemoProducts::CompositeProductId, "aa11", index) == BoardSelection::Chosen,
		"a serial picks its board ignoring case");
	Expect(
		SelectBoard(one, DemoProducts::CompositeProductId, "BB22", index) == BoardSelection::SerialNotFound,
		"an unknown serial is not found");
	Expect(
		SelectBoard(one, DemoProducts::StorageProductId, "", index) == BoardSelection::NoBoard,
		"no storage board is reported as none");
	Expect(
		SelectBoard(one, DemoProducts::DfuProductId, "AA11", index) == BoardSelection::SerialOfOtherDemo,
		"a serial of another demo is reported as such");

	std::vector<BoardInfo> two = one;
	BoardInfo second;
	second.productId = DemoProducts::CompositeProductId;
	second.serial = "BB22";
	two.push_back(second);
	Expect(
		SelectBoard(two, DemoProducts::CompositeProductId, "", index) == BoardSelection::Several,
		"two composites without a serial are refused");
	Expect(
		SelectBoard(two, DemoProducts::CompositeProductId, "BB22", index) == BoardSelection::Chosen && index == 1,
		"the serial picks the second of two composites");

	BoardInfo noSerial;
	noSerial.productId = DemoProducts::CompositeProductId;
	std::vector<BoardInfo> withoutSerial = { noSerial };
	Expect(
		SelectBoard(withoutSerial, DemoProducts::CompositeProductId, "", index) == BoardSelection::Chosen,
		"one board without a serial is chosen without one");
	Expect(
		SelectBoard(withoutSerial, DemoProducts::CompositeProductId, "AA11", index) == BoardSelection::SerialNotFound,
		"a board with no serial is never picked by a serial");
}


void CheckStorageNode() {
	BoardInfo board;
	board.productId = DemoProducts::StorageProductId;
	board.serial = "0A1B2C";
	std::string reason;

	Expect(IsBoardStorageNode("usb\\vid_1209&pid_0003\\0a1b2c", board, reason), "the board's own node is accepted, serial ignoring case");
	Expect(reason.empty(), "an accepted node gives no reason");
	Expect(!IsBoardStorageNode("USB\\VID_1209&PID_0003\\0A1B2D", board, reason), "another serial is refused");
	Expect(reason.find("serial") != std::string::npos, "a refused serial says so");
	Expect(!IsBoardStorageNode("USB\\VID_1209&PID_0002\\0A1B2C", board, reason), "another demo's PID is refused");
	Expect(!IsBoardStorageNode("USB\\VID_046D&PID_0003\\0A1B2C", board, reason), "another vendor is refused");
	Expect(!IsBoardStorageNode("USB\\VID_1209&PID_0003&MI_00\\7&1A&0&0000", board, reason), "an interface node is not the board");
	Expect(!IsBoardStorageNode("USB\\VID_1209&PID_0003\\6&2A3B4C5&0&1", board, reason), "a node that Windows named, with no serial, is refused");
	Expect(!IsBoardStorageNode("HID\\VID_1209&PID_0003\\0A1B2C", board, reason), "a node on another bus is refused");
	Expect(!IsBoardStorageNode("USBSTOR\\DISK&VEN_VHAL\\0A1B2C&0", board, reason), "a disk instance is not a USB device node");

	BoardInfo noSerial;
	noSerial.productId = DemoProducts::StorageProductId;
	Expect(!IsBoardStorageNode("USB\\VID_1209&PID_0003\\0A1B2C", noSerial, reason), "a board with no serial is never matched");
	Expect(!reason.empty(), "a board with no serial gives a reason");

	BoardInfo composite;
	composite.productId = DemoProducts::CompositeProductId;
	composite.serial = "0A1B2C";
	Expect(!IsBoardStorageNode("USB\\VID_1209&PID_0002\\0A1B2C", composite, reason), "a composite is not a storage board");
}


void CheckNodeOfBoard() {
	BoardInfo audio;
	audio.productId = DemoProducts::AudioProductId;
	audio.serial = "0A1B2C";
	std::string reason;
	Expect(DemoNameOf(DemoProducts::AudioProductId) == "USBAudio", "PID 0006 is USBAudio");
	Expect(IsNodeOfBoard("USB\\VID_1209&PID_0006\\0A1B2C", audio, reason), "the board's own device node matches");
	Expect(IsNodeOfBoard("usb\\vid_1209&pid_0006\\0a1b2c", audio, reason), "the node matches ignoring case");
	Expect(reason.empty(), "a match gives no reason");
	Expect(!IsNodeOfBoard("USB\\VID_1209&PID_0006&MI_00\\7&1A&0&0000", audio, reason), "an interface of the board is not its node");
	Expect(!IsNodeOfBoard("USB\\VID_1209&PID_0006\\0A1B2D", audio, reason), "another board's serial does not match");
	Expect(!reason.empty(), "a refused node gives a reason");
	Expect(!IsNodeOfBoard("USB\\VID_1209&PID_0002\\0A1B2C", audio, reason), "a composite with the same serial is not the audio board");
	Expect(!IsNodeOfBoard("USB\\VID_046D&PID_0006\\0A1B2C", audio, reason), "another vendor with the same PID is not the board");
	Expect(!IsNodeOfBoard("PCI\\VEN_1209&DEV_0006\\0A1B2C", audio, reason), "a non-USB device is not the board");

	BoardInfo noSerial;
	noSerial.productId = DemoProducts::AudioProductId;
	Expect(!IsNodeOfBoard("USB\\VID_1209&PID_0006\\0A1B2C", noSerial, reason), "a board with no serial is never matched");
}


void CheckContainerIds() {
	const std::string board = "{8F3A1C2D-0000-4000-8000-000000000001}";
	const std::string other = "{8F3A1C2D-0000-4000-8000-000000000002}";
	const std::string nullId = "{00000000-0000-0000-FFFF-FFFFFFFFFFFF}";

	Expect(ContainerIdMatches(board, "{8f3a1c2d-0000-4000-8000-000000000001}"), "the same container ID matches ignoring case");
	Expect(ContainerIdMatches(board, "8F3A1C2D-0000-4000-8000-000000000001"), "braces are optional on either side");
	Expect(!ContainerIdMatches(board, other), "another device's container ID does not match");
	Expect(!ContainerIdMatches(board, ""), "an endpoint with no container ID does not match");
	Expect(!ContainerIdMatches(board, nullId), "the null container ID never matches");
	Expect(!ContainerIdMatches(nullId, nullId), "two null container IDs never match");
	Expect(!ContainerIdMatches("", ""), "two empty container IDs never match");
	Expect(!ContainerIdMatches("", board), "a board with no container ID matches nothing");
	Expect(!HasContainerId("{00000000-0000-0000-ffff-ffffffffffff}"), "the null container ID is not a group, whatever the case");
	Expect(!HasContainerId(""), "an empty container ID is not a group");
	Expect(HasContainerId(board), "a GUID container ID is a group");

	std::vector<std::string> endpoints = { other, board, "{8F3A1C2D-0000-4000-8000-000000000003}" };
	std::vector<std::size_t> one = { 1 };
	Expect(MatchingEndpoints(board, endpoints) == one, "the endpoint with the board's container ID is picked by index");

	std::vector<std::string> twice = { board, other, "{8f3a1c2d-0000-4000-8000-000000000001}" };
	std::vector<std::size_t> both = { 0, 2 };
	Expect(MatchingEndpoints(board, twice) == both, "two endpoints with the board's container ID are both listed");

	std::vector<std::string> none = { other, nullId };
	Expect(MatchingEndpoints(board, none).empty(), "no endpoint with the board's container ID gives an empty list");
	Expect(MatchingEndpoints(nullId, { nullId, nullId }).empty(), "a board with the null container ID matches no endpoint");
}


void CheckBoardLine() {
	BoardInfo board;
	board.productId = DemoProducts::StorageProductId;
	board.serial = "0A1B2C";
	Expect(BoardLine(board) == "  0003  USBStorage   serial 0A1B2C", "the list line has PID, demo and serial");

	BoardInfo noSerial;
	noSerial.productId = DemoProducts::DfuProductId;
	Expect(BoardLine(noSerial).find("(no serial number)") != std::string::npos, "a board without a serial says so");
}


}


int main() {
	CheckDemoNames();
	CheckInterfacePaths();
	CheckParseInstance();
	CheckSerialMatching();
	CheckSelection();
	CheckBoardLine();
	CheckStorageNode();
	CheckNodeOfBoard();
	CheckContainerIds();

	std::printf("usbclasscheck device identity test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
