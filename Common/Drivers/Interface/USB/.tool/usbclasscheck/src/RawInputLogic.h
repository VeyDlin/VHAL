#pragma once

#include <cstdint>
#include <string>
#include <vector>

// One keyboard event from Raw Input: the virtual key and whether it is a break (release) or a make (press).
struct KeyEvent {
	std::uint16_t virtualKey = 0;
	bool breakCode = false;
};

// One mouse event from Raw Input, in raw counts. The wheel is in the units Raw Input gives, which are not steps.
struct MouseEvent {
	std::int32_t dx = 0;
	std::int32_t dy = 0;
	std::int32_t wheel = 0;
};

// VK_F13 is 0x7C in winuser.h, and VK_F24 follows it, so F13 to F24 are twelve consecutive codes.
inline constexpr std::uint16_t FirstFunctionKeyVirtual = 0x7C;
inline constexpr std::uint16_t FunctionKeyCount = 12;

// The name the key command takes for the function key at index (0 is f13).
std::string FunctionKeyName(std::uint16_t index);

// The key check: the events must be a make and then a break of F13, then of F14, up to F24, and nothing else.
bool CheckFunctionKeys(const std::vector<KeyEvent>& observed, std::string& detail);

// The mouse check. The legs are the movements the commands sent. The movement must come back as sent: the positive
// and the negative counts of each axis match the legs. The wheel must be seen in both directions and net to zero.
bool CheckMotionReturned(
	const std::vector<MouseEvent>& legs,
	const std::vector<MouseEvent>& observed,
	std::string& detail);
