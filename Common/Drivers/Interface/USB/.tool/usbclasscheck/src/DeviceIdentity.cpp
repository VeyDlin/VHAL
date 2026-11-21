#include "DeviceIdentity.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "DemoProducts.h"
#include "Text.h"

namespace {

// Reads a hexadecimal number of one to eight digits. Anything else fails.
bool ParseHex(const std::string& text, std::uint32_t& value) {
	if (text.empty() || text.size() > 8) {
		return false;
	}

	std::uint32_t result = 0;
	for (char digit : text) {
		std::uint32_t nibble = 0;
		if (digit >= '0' && digit <= '9') {
			nibble = static_cast<std::uint32_t>(digit - '0');
		} else if (digit >= 'A' && digit <= 'F') {
			nibble = static_cast<std::uint32_t>(digit - 'A' + 10);
		} else if (digit >= 'a' && digit <= 'f') {
			nibble = static_cast<std::uint32_t>(digit - 'a' + 10);
		} else {
			return false;
		}

		result = result * 16 + nibble;
	}

	value = result;
	return true;
}


// Splits text at every occurrence of the separator. Empty pieces are kept, so the count of pieces is the count of separators plus one.
std::vector<std::string> SplitAt(const std::string& text, char separator) {
	std::vector<std::string> pieces;
	std::string current;
	for (char character : text) {
		if (character == separator) {
			pieces.push_back(current);
			current.clear();
		} else {
			current.push_back(character);
		}
	}

	pieces.push_back(current);
	return pieces;
}


const std::uint16_t DemoProductIds[] = {
	DemoProducts::BenchProductId,
	DemoProducts::CompositeProductId,
	DemoProducts::StorageProductId,
	DemoProducts::DfuProductId,
	DemoProducts::NetworkProductId,
	DemoProducts::AudioProductId
};

// The null container ID, which Windows gives a device it cannot group, without its braces.
const char NullContainerIdText[] = "00000000-0000-0000-FFFF-FFFFFFFFFFFF";


// The container ID's GUID text without the braces, so both forms compare the same way.
std::string BareContainerId(const std::string& containerId) {
	std::string bare = containerId;
	if (!bare.empty() && bare.front() == '{') {
		bare.erase(0, 1);
	}

	if (!bare.empty() && bare.back() == '}') {
		bare.pop_back();
	}

	return bare;
}

}


std::string DemoNameOf(std::uint16_t productId) {
	if (productId == DemoProducts::BenchProductId) {
		return "USBBench";
	}

	if (productId == DemoProducts::CompositeProductId) {
		return "USBComposite";
	}

	if (productId == DemoProducts::StorageProductId) {
		return "USBStorage";
	}

	if (productId == DemoProducts::DfuProductId) {
		return "USBDfu";
	}

	if (productId == DemoProducts::NetworkProductId) {
		return "USBNetwork";
	}

	if (productId == DemoProducts::AudioProductId) {
		return "USBAudio";
	}

	return "";
}


bool IsDemoProduct(std::uint16_t productId) {
	return !DemoNameOf(productId).empty();
}


std::string BoardLine(const BoardInfo& board) {
	std::string text;
	AppendFormat(
		text,
		"  %04X  %-12s serial ",
		static_cast<unsigned int>(board.productId),
		DemoNameOf(board.productId).c_str());
	text += board.serial.empty() ? "(no serial number)" : board.serial;
	return text;
}


std::string InstanceFromInterfacePath(const std::string& path) {
	std::string text = path;
	if (text.rfind("\\\\?\\", 0) == 0) {
		text.erase(0, 4);
	}

	std::vector<std::string> pieces = SplitAt(text, '#');
	if (!pieces.empty() && !pieces.back().empty() && pieces.back()[0] == '{') {
		pieces.pop_back();
	}

	if (pieces.size() < 2) {
		return "";
	}

	std::string instance = pieces[0];
	for (std::size_t index = 1; index < pieces.size(); index++) {
		instance += "\\" + pieces[index];
	}

	return instance;
}


bool ParseInstance(const std::string& instance, InstanceParts& parts) {
	std::size_t firstSeparator = instance.find('\\');
	if (firstSeparator == std::string::npos) {
		return false;
	}

	std::size_t secondSeparator = instance.find('\\', firstSeparator + 1);
	if (secondSeparator == std::string::npos) {
		return false;
	}

	std::string upper = UpperAscii(instance.substr(0, secondSeparator));
	std::vector<std::string> ids = SplitAt(upper.substr(firstSeparator + 1), '&');
	if (ids.size() < 2 || ids.size() > 3) {
		return false;
	}

	if (ids[0].rfind("VID_", 0) != 0 || ids[1].rfind("PID_", 0) != 0) {
		return false;
	}

	std::uint32_t vendorId = 0;
	std::uint32_t productId = 0;
	if (!ParseHex(ids[0].substr(4), vendorId) || !ParseHex(ids[1].substr(4), productId)) {
		return false;
	}

	int interfaceNumber = -1;
	if (ids.size() == 3) {
		if (ids[2].rfind("MI_", 0) != 0) {
			return false;
		}

		std::uint32_t number = 0;
		if (!ParseHex(ids[2].substr(3), number)) {
			return false;
		}

		interfaceNumber = static_cast<int>(number);
	}

	parts.bus = upper.substr(0, firstSeparator);
	parts.vendorId = static_cast<std::uint16_t>(vendorId);
	parts.productId = static_cast<std::uint16_t>(productId);
	parts.interfaceNumber = interfaceNumber;
	parts.segment = instance.substr(secondSeparator + 1);
	return true;
}


bool IsBoardNode(const InstanceParts& parts) {
	return parts.bus == "USB" && parts.interfaceNumber < 0 && !parts.segment.empty();
}


std::string SerialOfBoardNode(const InstanceParts& parts) {
	if (parts.segment.find('&') != std::string::npos) {
		return "";
	}

	return parts.segment;
}


bool SerialMatches(const std::string& wanted, const std::string& found) {
	if (wanted.empty()) {
		return true;
	}

	return EqualsIgnoreCase(wanted, found);
}


bool IsBoardStorageNode(const std::string& instance, const BoardInfo& board, std::string& reason) {
	reason.clear();
	if (board.serial.empty()) {
		reason = "the board has no serial number, so its disk cannot be told from the disk of another board";
		return false;
	}

	if (board.productId != DemoProducts::StorageProductId) {
		reason = "the board is not the storage demo";
		return false;
	}

	InstanceParts parts;
	if (!ParseInstance(instance, parts) || !IsBoardNode(parts)) {
		reason = "the disk is not below a USB device";
		return false;
	}

	if (parts.vendorId != DemoProducts::VendorId || parts.productId != DemoProducts::StorageProductId) {
		AppendFormat(
			reason,
			"the USB device above the disk is %04X:%04X, not the storage demo",
			static_cast<unsigned int>(parts.vendorId),
			static_cast<unsigned int>(parts.productId));
		return false;
	}

	if (!EqualsIgnoreCase(SerialOfBoardNode(parts), board.serial)) {
		reason = "the USB device above the disk has the serial '" + parts.segment + "', not the board's";
		return false;
	}

	return true;
}


bool IsNodeOfBoard(const std::string& instance, const BoardInfo& board, std::string& reason) {
	reason.clear();
	if (board.serial.empty()) {
		reason = "the board has no serial number, so it cannot be told from another board";
		return false;
	}

	InstanceParts parts;
	if (!ParseInstance(instance, parts) || !IsBoardNode(parts)) {
		reason = "the device is not a USB device node";
		return false;
	}

	if (parts.vendorId != DemoProducts::VendorId || parts.productId != board.productId) {
		AppendFormat(
			reason,
			"the USB device is %04X:%04X, not this board",
			static_cast<unsigned int>(parts.vendorId),
			static_cast<unsigned int>(parts.productId));
		return false;
	}

	if (!EqualsIgnoreCase(SerialOfBoardNode(parts), board.serial)) {
		reason = "the USB device has the serial '" + parts.segment + "', not the board's";
		return false;
	}

	return true;
}


bool HasContainerId(const std::string& containerId) {
	std::string bare = BareContainerId(containerId);
	return !bare.empty() && !EqualsIgnoreCase(bare, NullContainerIdText);
}


bool ContainerIdMatches(const std::string& boardContainer, const std::string& endpointContainer) {
	if (!HasContainerId(boardContainer) || !HasContainerId(endpointContainer)) {
		return false;
	}

	return EqualsIgnoreCase(BareContainerId(boardContainer), BareContainerId(endpointContainer));
}


std::vector<std::size_t> MatchingEndpoints(const std::string& boardContainer, const std::vector<std::string>& endpointContainers) {
	std::vector<std::size_t> matches;
	if (!HasContainerId(boardContainer)) {
		return matches;
	}

	for (std::size_t index = 0; index < endpointContainers.size(); index++) {
		if (ContainerIdMatches(boardContainer, endpointContainers[index])) {
			matches.push_back(index);
		}
	}

	return matches;
}


BoardSelection SelectBoard(
	const std::vector<BoardInfo>& boards,
	std::uint16_t productId,
	const std::string& serial,
	std::size_t& index)
{
	if (!serial.empty()) {
		bool otherDemo = false;
		bool found = false;
		for (std::size_t position = 0; position < boards.size(); position++) {
			if (boards[position].serial.empty() || !EqualsIgnoreCase(boards[position].serial, serial)) {
				continue;
			}

			if (boards[position].productId == productId) {
				if (found) {
					return BoardSelection::Several;
				}

				found = true;
				index = position;
			} else {
				otherDemo = true;
			}
		}

		if (found) {
			return BoardSelection::Chosen;
		}

		return otherDemo ? BoardSelection::SerialOfOtherDemo : BoardSelection::SerialNotFound;
	}

	std::size_t count = 0;
	for (std::size_t position = 0; position < boards.size(); position++) {
		if (boards[position].productId == productId) {
			if (count == 0) {
				index = position;
			}

			count++;
		}
	}

	if (count == 0) {
		return BoardSelection::NoBoard;
	}

	if (count > 1) {
		return BoardSelection::Several;
	}

	return BoardSelection::Chosen;
}
