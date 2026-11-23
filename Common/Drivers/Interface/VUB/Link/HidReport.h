#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>

namespace VUB {


// The report of a HID link, the same on the device and on the PC: byte 0 is the length of the payload, from 1 to the report's size
// minus 1, the payload follows it, and the rest of the report is zero. The report has no report ID: a PC API that puts the ID in
// front of the report (ID 0) adds it outside this layout.


// Writes the payload into the report and pads it with zeros. Returns the report's size, or invalidParameter for an empty payload,
// a payload that does not fit after the length byte, or one longer than 255 bytes.
inline Result<std::size_t> HidReportPack(std::span<const uint8> payload, std::span<uint8> report) {
	if (report.size() < 2 || payload.empty() || payload.size() > report.size() - 1 || payload.size() > 255) {
		return ResultStatus::invalidParameter;
	}

	report[0] = static_cast<uint8>(payload.size());
	for (std::size_t index = 0; index < payload.size(); index++) {
		report[1 + index] = payload[index];
	}

	for (std::size_t index = 1 + payload.size(); index < report.size(); index++) {
		report[index] = 0;
	}

	return report.size();
}


// The payload of a received report. A report whose length byte is 0, or larger than the bytes after it, is invalidData. The
// padding is not checked.
inline Result<std::span<const uint8>> HidReportPayload(std::span<const uint8> report) {
	if (report.empty() || report[0] == 0 || report[0] > report.size() - 1) {
		return ResultStatus::invalidData;
	}

	return report.subspan(1, report[0]);
}

}
