#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>


// Report descriptor of a HID interface, built at compile time with the same technique as the device descriptors. The chain is
// written in a captureless lambda and the items are encoded in the shortest form HID 1.11 section 6.2.2.2 allows: a value of 0
// takes no data byte, other values take 1, 2 or 4 bytes, and the logical and physical ranges are signed.
//
//   using KeyboardReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootKeyboard(); }>;
//
// KeyboardReport::Info is what the HID class builder and the HID function take. Rules that are violated are compile errors with
// their message: unbalanced collections, report ID 0, reports with and without IDs in one descriptor, a report whose bits are not
// a whole number of bytes, and a report longer than 64 bytes (one full-speed packet).


// Type of a report: the wire value of the report type in GET_REPORT and SET_REPORT (the high byte of wValue).
enum class USBHidReportType : uint8 {
	Input = 1,
	Output = 2,
	Feature = 3
};


// Kinds of collection (HID 1.11 section 6.2.2.6). Vendor kinds are 0x80 to 0xFF and are given as a cast.
enum class USBHidCollection : uint8 {
	Physical = 0x00,
	Application = 0x01,
	Logical = 0x02,
	Report = 0x03,
	NamedArray = 0x04,
	UsageSwitch = 0x05,
	UsageModifier = 0x06
};


// Flag bits of Input, Output and Feature (HID 1.11 section 6.2.2.4). Each pair is one bit: the zero value is the first name.
struct USBHidFlags {
	static constexpr uint16 Data = 0x0000;
	static constexpr uint16 Constant = 0x0001;
	static constexpr uint16 Array = 0x0000;
	static constexpr uint16 Variable = 0x0002;
	static constexpr uint16 Absolute = 0x0000;
	static constexpr uint16 Relative = 0x0004;
	static constexpr uint16 NoWrap = 0x0000;
	static constexpr uint16 Wrap = 0x0008;
	static constexpr uint16 Linear = 0x0000;
	static constexpr uint16 NonLinear = 0x0010;
	static constexpr uint16 Preferred = 0x0000;
	static constexpr uint16 NoPreferred = 0x0020;
	static constexpr uint16 NoNullPosition = 0x0000;
	static constexpr uint16 NullState = 0x0040;
	static constexpr uint16 NonVolatile = 0x0000;
	static constexpr uint16 Volatile = 0x0080;
	static constexpr uint16 BitField = 0x0000;
	static constexpr uint16 BufferedBytes = 0x0100;
};


// One report of the descriptor: its type, its ID (0 when the descriptor has no IDs) and its length on the wire, including the
// report ID byte when the descriptor uses IDs.
struct USBHidReportEntry {
	USBHidReportType type = USBHidReportType::Input;
	uint8 reportId = 0;
	std::size_t length = 0;
};


constexpr std::size_t USBHidMaxReportLength = 64;
constexpr std::size_t USBHidMaxReports = 16;


// The reports of a descriptor with their lengths, and the descriptor bytes. The builder computes it, so the function and the
// class builder that wrote the descriptor agree on every size. A report that is not in the table has length 0.
struct USBHidReportInfo {
	std::span<const uint8> descriptor;
	std::array<USBHidReportEntry, USBHidMaxReports> reports{};
	std::size_t reportCount = 0;


	constexpr std::size_t Length(USBHidReportType type, uint8 reportId) const {
		for (std::size_t index = 0; index < reportCount; index++) {
			const USBHidReportEntry& entry = reports[index];
			if (entry.type == type && entry.reportId == reportId) {
				return entry.length;
			}
		}

		return 0;
	}


	constexpr std::size_t InputLength(uint8 reportId) const {
		return Length(USBHidReportType::Input, reportId);
	}


	constexpr std::size_t OutputLength(uint8 reportId) const {
		return Length(USBHidReportType::Output, reportId);
	}


	constexpr std::size_t FeatureLength(uint8 reportId) const {
		return Length(USBHidReportType::Feature, reportId);
	}


	// The largest report of one type, including its ID byte. 0 when the descriptor has none of that type.
	constexpr std::size_t MaxLength(USBHidReportType type) const {
		std::size_t largest = 0;
		for (std::size_t index = 0; index < reportCount; index++) {
			if (reports[index].type == type && reports[index].length > largest) {
				largest = reports[index].length;
			}
		}

		return largest;
	}


	constexpr bool UsesReportIds() const {
		for (std::size_t index = 0; index < reportCount; index++) {
			if (reports[index].reportId != 0) {
				return true;
			}
		}

		return false;
	}
};


// What the builder produces: the descriptor bytes (USBBuildBuffer, of which size bytes are used) and the report table.
struct USBHidReportBuilt {
	USBBuildBuffer bytes;
	std::array<USBHidReportEntry, USBHidMaxReports> reports{};
	std::size_t reportCount = 0;
};


class USBHidReportBuilder {
private:
	// Item types (HID 1.11 section 6.2.2.1) and the tags of each type.
	static constexpr uint8 MainType = 0;
	static constexpr uint8 GlobalType = 1;
	static constexpr uint8 LocalType = 2;
	static constexpr uint8 InputTag = 0x8;
	static constexpr uint8 OutputTag = 0x9;
	static constexpr uint8 FeatureTag = 0xB;
	static constexpr uint8 CollectionTag = 0xA;
	static constexpr uint8 EndCollectionTag = 0xC;
	static constexpr uint8 UsagePageTag = 0x0;
	static constexpr uint8 LogicalMinimumTag = 0x1;
	static constexpr uint8 LogicalMaximumTag = 0x2;
	static constexpr uint8 PhysicalMinimumTag = 0x3;
	static constexpr uint8 PhysicalMaximumTag = 0x4;
	static constexpr uint8 UnitExponentTag = 0x5;
	static constexpr uint8 UnitTag = 0x6;
	static constexpr uint8 ReportSizeTag = 0x7;
	static constexpr uint8 ReportIdTag = 0x8;
	static constexpr uint8 ReportCountTag = 0x9;
	static constexpr uint8 UsageTag = 0x0;
	static constexpr uint8 UsageMinimumTag = 0x1;
	static constexpr uint8 UsageMaximumTag = 0x2;
	USBBuildBuffer body;
	uint32 reportSize = 0;
	uint32 reportCount = 0;
	uint8 reportId = 0;
	bool plainReport = false;
	bool identifiedReport = false;
	std::size_t collectionDepth = 0;
	// The bits of each report, summed over its items, in the order the reports first appear. bits[index] belongs to entries[index].
	std::array<USBHidReportEntry, USBHidMaxReports> entries{};
	std::array<uint64, USBHidMaxReports> bits{};
	std::size_t entryCount = 0;


public:
	// HID 1.11 Appendix B.1: 8 bytes in (modifiers, reserved, six keys), 1 byte out (five LEDs and padding). No report ID.
	static constexpr USBHidReportBuilder BootKeyboard() {
		USBHidReportBuilder builder;
		builder.UsagePage(0x01).Usage(0x06).Collection(USBHidCollection::Application)
			.UsagePage(0x07).UsageMinimum(0xE0).UsageMaximum(0xE7)
			.LogicalMinimum(0).LogicalMaximum(1).ReportSize(1).ReportCount(8).Input(USBHidFlags::Data | USBHidFlags::Variable)
			.ReportCount(1).ReportSize(8).Input(USBHidFlags::Constant)
			.ReportCount(5).ReportSize(1).UsagePage(0x08).UsageMinimum(1).UsageMaximum(5).Output(USBHidFlags::Data | USBHidFlags::Variable)
			.ReportCount(1).ReportSize(3).Output(USBHidFlags::Constant)
			.ReportCount(6).ReportSize(8).LogicalMinimum(0).LogicalMaximum(101).UsagePage(0x07).UsageMinimum(0).UsageMaximum(101)
			.Input(USBHidFlags::Array)
			.EndCollection();
		return builder;
	}


	// HID 1.11 Appendix B.2 with a wheel: 4 bytes in (buttons, X, Y, wheel). The first three bytes are the boot report, so a boot
	// protocol host reads buttons, X and Y, and a report protocol host also gets the wheel. No report ID.
	static constexpr USBHidReportBuilder BootMouse() {
		USBHidReportBuilder builder;
		builder.UsagePage(0x01).Usage(0x02).Collection(USBHidCollection::Application)
			.Usage(0x01).Collection(USBHidCollection::Physical)
			.UsagePage(0x09).UsageMinimum(1).UsageMaximum(3)
			.LogicalMinimum(0).LogicalMaximum(1).ReportCount(3).ReportSize(1).Input(USBHidFlags::Data | USBHidFlags::Variable)
			.ReportCount(1).ReportSize(5).Input(USBHidFlags::Constant)
			.UsagePage(0x01).Usage(0x30).Usage(0x31).LogicalMinimum(-127).LogicalMaximum(127)
			.ReportSize(8).ReportCount(2).Input(USBHidFlags::Variable | USBHidFlags::Relative)
			.Usage(0x38).ReportCount(1).Input(USBHidFlags::Variable | USBHidFlags::Relative)
			.EndCollection()
			.EndCollection();
		return builder;
	}


	// One 16-bit consumer usage per report, as a keyboard's media keys send it. Usages 0x0000 to 0x03FF. No report ID.
	static constexpr USBHidReportBuilder ConsumerControl() {
		USBHidReportBuilder builder;
		builder.UsagePage(0x0C).Usage(0x01).Collection(USBHidCollection::Application)
			.LogicalMinimum(0).LogicalMaximum(0x03FF).UsageMinimum(0).UsageMaximum(0x03FF)
			.ReportSize(16).ReportCount(1).Input(USBHidFlags::Array)
			.EndCollection();
		return builder;
	}


	// A vendor report of bytes in and bytes out, on usage page 0xFF00 (usage 0x01 in, 0x02 out). No report ID. Every OS opens a
	// vendor HID interface without a driver, so it is a raw data channel next to a bulk one.
	static constexpr USBHidReportBuilder VendorRaw(uint8 bytes) {
		Require<USBText{"a vendor report is 1 to 64 bytes"}>(bytes >= 1 && bytes <= USBHidMaxReportLength);
		USBHidReportBuilder builder;
		builder.UsagePage(0xFF00).Usage(0x01).Collection(USBHidCollection::Application)
			.Usage(0x01).LogicalMinimum(0).LogicalMaximum(255).ReportSize(8).ReportCount(bytes).Input(USBHidFlags::Data | USBHidFlags::Variable)
			.Usage(0x02).ReportCount(bytes).Output(USBHidFlags::Data | USBHidFlags::Variable)
			.EndCollection();
		return builder;
	}


	constexpr USBHidReportBuilder& UsagePage(uint16 page) {
		PutUnsigned(GlobalType, UsagePageTag, page);
		return *this;
	}


	constexpr USBHidReportBuilder& Usage(uint32 usage) {
		PutUnsigned(LocalType, UsageTag, usage);
		return *this;
	}


	constexpr USBHidReportBuilder& UsageMinimum(uint32 usage) {
		PutUnsigned(LocalType, UsageMinimumTag, usage);
		return *this;
	}


	constexpr USBHidReportBuilder& UsageMaximum(uint32 usage) {
		PutUnsigned(LocalType, UsageMaximumTag, usage);
		return *this;
	}


	constexpr USBHidReportBuilder& Collection(USBHidCollection kind) {
		uint8 value = static_cast<uint8>(kind);
		Require<USBText{"a collection kind is 0 to 6 or vendor 0x80 to 0xFF"}>(value <= 6 || value >= 0x80);
		Require<USBText{"collections nest at most 255 deep"}>(collectionDepth < 255);
		PutUnsigned(MainType, CollectionTag, value);
		collectionDepth++;
		return *this;
	}


	constexpr USBHidReportBuilder& EndCollection() {
		Require<USBText{"EndCollection without a Collection"}>(collectionDepth > 0);
		collectionDepth--;
		PutUnsigned(MainType, EndCollectionTag, 0);
		return *this;
	}


	constexpr USBHidReportBuilder& LogicalMinimum(int32 value) {
		PutSigned(GlobalType, LogicalMinimumTag, value);
		return *this;
	}


	constexpr USBHidReportBuilder& LogicalMaximum(int32 value) {
		PutSigned(GlobalType, LogicalMaximumTag, value);
		return *this;
	}


	constexpr USBHidReportBuilder& PhysicalMinimum(int32 value) {
		PutSigned(GlobalType, PhysicalMinimumTag, value);
		return *this;
	}


	constexpr USBHidReportBuilder& PhysicalMaximum(int32 value) {
		PutSigned(GlobalType, PhysicalMaximumTag, value);
		return *this;
	}


	// Power of ten of the unit, -8 to 7 (HID 1.11 section 6.2.2.7).
	constexpr USBHidReportBuilder& UnitExponent(int32 exponent) {
		Require<USBText{"unit exponent is -8 to 7"}>(exponent >= -8 && exponent <= 7);
		PutSigned(GlobalType, UnitExponentTag, exponent);
		return *this;
	}


	constexpr USBHidReportBuilder& Unit(uint32 unit) {
		PutUnsigned(GlobalType, UnitTag, unit);
		return *this;
	}


	// Size of one field in bits, 1 to 32. Set before Input, Output or Feature.
	constexpr USBHidReportBuilder& ReportSize(uint32 bits) {
		PutUnsigned(GlobalType, ReportSizeTag, bits);
		reportSize = bits;
		return *this;
	}


	// Number of fields of the next main item. Set before Input, Output or Feature.
	constexpr USBHidReportBuilder& ReportCount(uint32 count) {
		PutUnsigned(GlobalType, ReportCountTag, count);
		reportCount = count;
		return *this;
	}


	// Report ID that the following reports carry. 0 is reserved, and a descriptor either uses IDs in every report or in none.
	constexpr USBHidReportBuilder& ReportId(uint8 id) {
		Require<USBText{"report ID 0 is reserved"}>(id != 0);
		PutUnsigned(GlobalType, ReportIdTag, id);
		reportId = id;
		return *this;
	}


	constexpr USBHidReportBuilder& Input(uint16 flags) {
		AddReport(USBHidReportType::Input, InputTag, flags);
		return *this;
	}


	constexpr USBHidReportBuilder& Output(uint16 flags) {
		AddReport(USBHidReportType::Output, OutputTag, flags);
		return *this;
	}


	constexpr USBHidReportBuilder& Feature(uint16 flags) {
		AddReport(USBHidReportType::Feature, FeatureTag, flags);
		return *this;
	}


	// Checks the whole descriptor and returns its bytes and its report table. Called by USBHidReportDescriptor.
	constexpr USBHidReportBuilt Build() const {
		Require<USBText{"a collection is not closed"}>(collectionDepth == 0);
		Require<USBText{"a report descriptor has at least one report"}>(entryCount > 0);
		USBHidReportBuilt built;
		built.bytes = body;
		built.reportCount = entryCount;
		for (std::size_t index = 0; index < entryCount; index++) {
			Require<USBText{"a report is not a whole number of bytes"}>(bits[index] % 8 == 0);
			std::size_t idBytes = entries[index].reportId != 0 ? 1 : 0;
			std::size_t length = idBytes + static_cast<std::size_t>(bits[index] / 8);
			Require<USBText{"a report is at most 64 bytes, one full-speed packet"}>(length <= USBHidMaxReportLength);
			built.reports[index] = entries[index];
			built.reports[index].length = length;
		}

		return built;
	}


private:
	// Writes one item with the prefix of HID 1.11 section 6.2.2.2: tag in bits 7..4, type in bits 3..2, data size code in bits 1..0.
	constexpr void PutItem(uint8 type, uint8 tag, std::size_t bytes, uint32 value) {
		body.Put(static_cast<uint8>((tag << 4) | (type << 2) | SizeCode(bytes)));
		for (std::size_t index = 0; index < bytes; index++) {
			body.Put(static_cast<uint8>(value >> (8 * index)));
		}
	}


	// Writes an unsigned item in the shortest form: no data byte for 0, otherwise 1, 2 or 4 bytes.
	constexpr void PutUnsigned(uint8 type, uint8 tag, uint32 value) {
		std::size_t bytes = 4;
		if (value == 0) {
			bytes = 0;
		} else if (value <= 0xFF) {
			bytes = 1;
		} else if (value <= 0xFFFF) {
			bytes = 2;
		}

		PutItem(type, tag, bytes, value);
	}


	// Writes a signed item in the shortest form that holds the value in two's complement: no data byte for 0, otherwise 1, 2 or 4.
	constexpr void PutSigned(uint8 type, uint8 tag, int32 value) {
		std::size_t bytes = 4;
		if (value == 0) {
			bytes = 0;
		} else if (value >= -128 && value <= 127) {
			bytes = 1;
		} else if (value >= -32768 && value <= 32767) {
			bytes = 2;
		}

		PutItem(type, tag, bytes, static_cast<uint32>(value));
	}


	static constexpr uint8 SizeCode(std::size_t bytes) {
		if (bytes == 0) {
			return 0;
		}

		if (bytes == 1) {
			return 1;
		}

		if (bytes == 2) {
			return 2;
		}

		return 3;
	}


	constexpr void AddReport(USBHidReportType type, uint8 tag, uint16 flags) {
		Require<USBText{"ReportSize and ReportCount are set before an Input, Output or Feature item"}>(reportSize > 0 && reportCount > 0);
		Require<USBText{"ReportSize is at most 32 bits"}>(reportSize <= 32);
		if (reportId == 0) {
			plainReport = true;
		} else {
			identifiedReport = true;
		}

		Require<USBText{"reports with and without IDs cannot be mixed in one descriptor"}>(!(plainReport && identifiedReport));
		PutUnsigned(MainType, tag, flags);
		AddBits(type, reportId, static_cast<uint64>(reportSize) * reportCount);
	}


	constexpr void AddBits(USBHidReportType type, uint8 id, uint64 count) {
		for (std::size_t index = 0; index < entryCount; index++) {
			if (entries[index].type == type && entries[index].reportId == id) {
				bits[index] += count;
				return;
			}
		}

		Require<USBText{"too many reports in one descriptor"}>(entryCount < USBHidMaxReports);
		entries[entryCount].type = type;
		entries[entryCount].reportId = id;
		bits[entryCount] = count;
		entryCount++;
	}
};


// The report table of a built descriptor, as a constant of the descriptor's own static storage.
constexpr USBHidReportInfo USBHidMakeReportInfo(std::span<const uint8> descriptor, const USBHidReportBuilt& built) {
	USBHidReportInfo info;
	info.descriptor = descriptor;
	info.reports = built.reports;
	info.reportCount = built.reportCount;
	return info;
}


// The parts of the chain Make builds, computed once per Make. A namespace-scope variable template, so the class below can use it in
// its static members (the same arrangement as USBBuiltParts in USBDescriptors.h).
template<USBHidReportBuilder (*Make)()>
inline constexpr USBHidReportBuilt USBHidReportParts = Make().Build();


// The static storage of one report descriptor, built from a chain. Bytes is the descriptor, and Info is what the HID class
// builder and the HID function take. Usage:
//
//   using KeyboardReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootKeyboard(); }>;
template<USBHidReportBuilder (*Make)()>
class USBHidReportDescriptor {
public:
	static constexpr std::array<uint8, USBHidReportParts<Make>.bytes.size> Bytes =
		USBBuildExact<USBHidReportParts<Make>.bytes.size>(USBHidReportParts<Make>.bytes);
	static constexpr USBHidReportInfo Info = USBHidMakeReportInfo(Bytes, USBHidReportParts<Make>);
};
