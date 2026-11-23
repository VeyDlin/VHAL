#include "SimCheck.h"
#include <Drivers/Interface/VUB/Link/HidReport.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// The layout written by hand: a 3-byte payload in an 8-byte report is the length, the payload and four zeros.
void CheckPack() {
	const std::array<uint8, 3> payload = {0x11, 0x00, 0x22};
	std::array<uint8, 8> report = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
	Result<std::size_t> packed = VUB::HidReportPack(payload, report);
	const std::array<uint8, 8> expected = {0x03, 0x11, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(packed.IsOk());
	SIM_EXPECT(packed.Value() == 8);
	SIM_EXPECT(report == expected);
}


// A payload that fills the report after the length byte fits; one byte more, or none, does not.
void CheckPackBounds() {
	std::array<uint8, 8> report{};
	const std::array<uint8, 7> full = {1, 2, 3, 4, 5, 6, 7};
	const std::array<uint8, 8> over = {1, 2, 3, 4, 5, 6, 7, 8};
	SIM_EXPECT(VUB::HidReportPack(full, report).IsOk());
	SIM_EXPECT(report[0] == 7 && report[7] == 7);
	SIM_EXPECT(VUB::HidReportPack(over, report).Error() == ResultStatus::invalidParameter);
	SIM_EXPECT(VUB::HidReportPack(std::span<const uint8>(), report).Error() == ResultStatus::invalidParameter);

	std::array<uint8, 300> large{};
	std::array<uint8, 256> payload{};
	SIM_EXPECT(VUB::HidReportPack(payload, large).Error() == ResultStatus::invalidParameter);
}


// A report gives back its payload; a length of 0, or one past the report, is not a packet. The padding is not read.
void CheckPayload() {
	const std::array<uint8, 8> report = {0x02, 0xAA, 0xBB, 0x55, 0x55, 0x55, 0x55, 0x55};
	Result<std::span<const uint8>> payload = VUB::HidReportPayload(report);
	SIM_EXPECT(payload.IsOk());
	SIM_EXPECT(payload.Value().size() == 2);
	SIM_EXPECT(payload.Value()[0] == 0xAA && payload.Value()[1] == 0xBB);

	const std::array<uint8, 8> empty = {0x00, 0xAA, 0, 0, 0, 0, 0, 0};
	const std::array<uint8, 8> beyond = {0x08, 1, 2, 3, 4, 5, 6, 7};
	const std::array<uint8, 8> last = {0x07, 1, 2, 3, 4, 5, 6, 7};
	SIM_EXPECT(VUB::HidReportPayload(empty).Error() == ResultStatus::invalidData);
	SIM_EXPECT(VUB::HidReportPayload(beyond).Error() == ResultStatus::invalidData);
	SIM_EXPECT(VUB::HidReportPayload(std::span<const uint8>()).Error() == ResultStatus::invalidData);
	SIM_EXPECT(VUB::HidReportPayload(last).IsOk() && VUB::HidReportPayload(last).Value().size() == 7);
}


// What one side packs, the other reads back, for every payload length of a 64-byte report.
void CheckRoundTrip() {
	std::array<uint8, 63> source{};
	for (std::size_t index = 0; index < source.size(); index++) {
		source[index] = static_cast<uint8>(index * 7 + 1);
	}

	bool same = true;
	for (std::size_t length = 1; length <= source.size(); length++) {
		std::array<uint8, 64> report{};
		Result<std::size_t> packed = VUB::HidReportPack(std::span<const uint8>(source.data(), length), report);
		Result<std::span<const uint8>> payload = VUB::HidReportPayload(report);
		if (!packed.IsOk() || !payload.IsOk() || payload.Value().size() != length) {
			same = false;
			continue;
		}

		for (std::size_t index = 0; index < length; index++) {
			same = same && payload.Value()[index] == source[index];
		}
	}

	SIM_EXPECT(same);
}


int main() {
	CheckPack();
	CheckPackBounds();
	CheckPayload();
	CheckRoundTrip();
	return SimFinish("TestVubHidReport");
}
