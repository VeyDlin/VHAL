#include "RawInputLogic.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Text.h"

namespace {

// The totals of one axis: the sum of its positive values and the sum of its negative values.
struct AxisTotals {
	std::int64_t positive = 0;
	std::int64_t negative = 0;
};


void AddToAxis(AxisTotals& totals, std::int64_t value) {
	if (value > 0) {
		totals.positive += value;
	} else if (value < 0) {
		totals.negative += value;
	}
}


std::string SignedText(std::int64_t value) {
	std::string text;
	AppendFormat(text, "%+lld", static_cast<long long>(value));
	return text;
}


std::string TotalsText(const AxisTotals& totals) {
	return SignedText(totals.positive) + "/" + SignedText(totals.negative);
}

}


std::string FunctionKeyName(std::uint16_t index) {
	std::string name = "f";
	std::string number;
	AppendFormat(number, "%u", static_cast<unsigned int>(13 + index));
	return name + number;
}


bool CheckFunctionKeys(const std::vector<KeyEvent>& observed, std::string& detail) {
	detail.clear();
	std::size_t expectedCount = static_cast<std::size_t>(FunctionKeyCount) * 2;
	if (observed.size() != expectedCount) {
		AppendFormat(
			detail,
			"%u key events from the board, expected %u (a make and a break for each of F13 to F24)",
			static_cast<unsigned int>(observed.size()),
			static_cast<unsigned int>(expectedCount));
		return false;
	}

	for (std::size_t index = 0; index < expectedCount; index++) {
		std::uint16_t key = static_cast<std::uint16_t>(FirstFunctionKeyVirtual + index / 2);
		bool expectBreak = (index % 2) == 1;
		const KeyEvent& event = observed[index];
		if (event.virtualKey != key || event.breakCode != expectBreak) {
			AppendFormat(
				detail,
				"event %u is virtual key 0x%02X %s, expected F%u %s",
				static_cast<unsigned int>(index + 1),
				static_cast<unsigned int>(event.virtualKey),
				event.breakCode ? "break" : "make",
				static_cast<unsigned int>(13 + index / 2),
				expectBreak ? "break" : "make");
			return false;
		}
	}

	detail = "F13 to F24, make and break each, in order";
	return true;
}


bool CheckMotionReturned(
	const std::vector<MouseEvent>& legs,
	const std::vector<MouseEvent>& observed,
	std::string& detail)
{
	AxisTotals expectedX;
	AxisTotals expectedY;
	for (const MouseEvent& leg : legs) {
		AddToAxis(expectedX, leg.dx);
		AddToAxis(expectedY, leg.dy);
	}

	AxisTotals seenX;
	AxisTotals seenY;
	AxisTotals seenWheel;
	for (const MouseEvent& event : observed) {
		AddToAxis(seenX, event.dx);
		AddToAxis(seenY, event.dy);
		AddToAxis(seenWheel, event.wheel);
	}

	bool movesOk = seenX.positive == expectedX.positive
		&& seenX.negative == expectedX.negative
		&& seenY.positive == expectedY.positive
		&& seenY.negative == expectedY.negative;
	bool wheelOk = seenWheel.positive > 0
		&& seenWheel.negative < 0
		&& seenWheel.positive + seenWheel.negative == 0;

	detail = "x " + TotalsText(seenX) + " (sent " + TotalsText(expectedX) + "), y " + TotalsText(seenY)
		+ " (sent " + TotalsText(expectedY) + "), wheel " + TotalsText(seenWheel) + " net "
		+ SignedText(seenWheel.positive + seenWheel.negative);
	return movesOk && wheelOk;
}
