#pragma once
#include "RegexError.h"
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>


// The limits of a program. Each one is a header constant, so a later change is one line.
constexpr std::size_t RegexMaxInstructions = 1024;
constexpr std::size_t RegexMaxClasses = 32;
constexpr std::size_t RegexMaxGroups = 15;
constexpr std::size_t RegexDefaultStackLimit = 4096;
// A class is a 256-bit bitmap, one bit for each byte value.
constexpr std::size_t RegexClassBytes = 32;
// The repeat maximum of an unbounded repeat (*, +, {m,}).
constexpr uint16 RegexUnbounded = 0xFFFF;
// A capture slot holds an offset in the input. A slot that did not take part in the match holds this value.
constexpr uint16 RegexSlotUnset = 0xFFFF;


// The operations of the program. Each instruction is one of them.
enum class RegexOperation : uint8 {
	character,      // matches the byte in x
	characterFold,  // matches the ASCII letter in arg, in either case
	any,            // matches any byte except LF and CR
	classMatch,     // matches a byte in the class with index x
	split,          // continues at x, then at y
	jump,           // continues at x
	begin,          // passes only at offset 0 of the input
	end,            // passes only at the end of the input
	save,           // stores the offset in slot arg
	clear,          // clears the slots from arg up to x (not included)
	match           // the pattern matches
};


// One instruction of a program. It is 6 bytes, so a program of 1024 instructions takes 6 KB of flash.
struct RegexInstruction {
	RegexOperation operation;
	uint8 arg;
	uint16 x;
	uint16 y;
};

static_assert(sizeof(RegexInstruction) == 6);


// The bitmap of a class: bit v is set when the class holds the byte value v.
using RegexClass = std::array<uint8, RegexClassBytes>;


// The program of a pattern, as the matching loops read it. The arrays live in flash. Count is the instruction count, and classes
// holds ClassCount bitmaps (one dummy bitmap when there are none). SlotCount is 2 * (groups + 1).
struct RegexProgramView {
	const RegexInstruction* code;
	std::size_t count;
	const RegexClass* classes;
	std::size_t slotCount;
};


// The arrays of a program of exact size. Failure is set when the second pass wrote a program that does not match its count.
template<std::size_t InstructionCount, std::size_t ClassCount>
struct RegexImage {
	std::array<RegexInstruction, InstructionCount> code{};
	std::array<RegexClass, ClassCount> classes{};
	RegexFailure failure{};
};


// The number of 32-bit words of the state set of a program, as the design counts them.
constexpr std::size_t RegexWordCount(std::size_t instructions) {
	return (instructions + 31) / 32;
}


// The state sets are kept in 16-bit words, two for each 32-bit word of the design. The bytes are the same, and 16-bit words keep the
// workspace structs free of padding, so their sizes are the formulas below.
constexpr std::size_t RegexStateWords(std::size_t instructions) {
	return 2 * RegexWordCount(instructions);
}


// The stack of the plain loop: two state sets and the closure stack of N pcs.
constexpr std::size_t RegexPlainStackBytes(std::size_t instructions) {
	return 8 * RegexWordCount(instructions) + 2 * instructions;
}


// The stack of the captures loop: two thread lists, each with the pcs, the slot values and the state set, and a closure stack of the
// given number of frames. RegexCaptureFrames gives that number from the program.
constexpr std::size_t RegexCapturesStackBytes(std::size_t instructions, std::size_t groups, std::size_t frames) {
	std::size_t slots = 2 * (groups + 1);
	return 2 * (2 * instructions + 2 * instructions * slots + 4 * RegexWordCount(instructions)) + 4 * frames;
}


// A frame of the closure stack of the captures loop. A pc frame holds the pc in key. A restore frame has RegexRestoreFlag set in key,
// the slot in the other bits, and the value to put back in value.
struct RegexFrame {
	uint16 key;
	uint16 value;
};


static_assert(sizeof(RegexFrame) == 4);


constexpr uint16 RegexRestoreFlag = 0x8000;
constexpr uint16 RegexSlotMask = 0x7FFF;


// The most frames that one closure of the captures loop can hold at once. The closure starts with one frame, for the start pc. Each pc
// is processed at most once per closure, because it is checked when it is popped. Processing a pc pushes frames: a jump, a begin or an
// end pushes one, a split pushes two, a save pushes a restore and the next pc, and a clear pushes a restore for each slot in its range
// and the next pc. A consuming pc and the match push nothing. Frames pending at any time never exceed the frames pushed, so this sum is
// a bound. The sum counts every pc of the program, reachable or not, so it can be larger than the pending frames of a run.
constexpr std::size_t RegexCaptureFrames(const RegexInstruction* code, std::size_t count) {
	std::size_t frames = 1;
	for (std::size_t pc = 0; pc < count; pc++) {
		const RegexInstruction& instruction = code[pc];
		if (instruction.operation == RegexOperation::jump || instruction.operation == RegexOperation::begin || instruction.operation == RegexOperation::end) {
			frames += 1;
		} else if (instruction.operation == RegexOperation::split || instruction.operation == RegexOperation::save) {
			frames += 2;
		} else if (instruction.operation == RegexOperation::clear) {
			std::size_t from = instruction.arg;
			std::size_t to = instruction.x;
			frames += 1 + (to > from ? to - from : 0);
		}
	}

	return frames;
}


// Sets the bit of a byte value.
constexpr void RegexSetBit(RegexClass& bits, std::size_t value) {
	bits[value / 8] = static_cast<uint8>(bits[value / 8] | (1u << (value % 8)));
}


// True when the bit of a byte value is set.
constexpr bool RegexHasBit(const RegexClass& bits, std::size_t value) {
	return (bits[value / 8] & (1u << (value % 8))) != 0;
}


// Adds every byte of other to bits.
constexpr void RegexUnite(RegexClass& bits, const RegexClass& other) {
	for (std::size_t index = 0; index < RegexClassBytes; index++) {
		bits[index] = static_cast<uint8>(bits[index] | other[index]);
	}
}


// Replaces the set with its complement.
constexpr void RegexComplement(RegexClass& bits) {
	for (std::size_t index = 0; index < RegexClassBytes; index++) {
		bits[index] = static_cast<uint8>(~bits[index]);
	}
}


// Adds the other case of every ASCII letter in the set.
constexpr void RegexFoldCase(RegexClass& bits) {
	for (std::size_t upper = 'A'; upper <= 'Z'; upper++) {
		std::size_t lower = upper + ('a' - 'A');
		if (RegexHasBit(bits, upper) || RegexHasBit(bits, lower)) {
			RegexSetBit(bits, upper);
			RegexSetBit(bits, lower);
		}
	}
}


// The shorthand classes. Each one adds its bytes to the set.
constexpr void RegexFillDigits(RegexClass& bits) {
	for (std::size_t value = '0'; value <= '9'; value++) {
		RegexSetBit(bits, value);
	}
}


constexpr void RegexFillWord(RegexClass& bits) {
	RegexFillDigits(bits);
	for (std::size_t value = 'A'; value <= 'Z'; value++) {
		RegexSetBit(bits, value);
		RegexSetBit(bits, value + ('a' - 'A'));
	}

	RegexSetBit(bits, '_');
}


constexpr void RegexFillSpace(RegexClass& bits) {
	RegexSetBit(bits, ' ');
	RegexSetBit(bits, '\t');
	RegexSetBit(bits, '\n');
	RegexSetBit(bits, '\v');
	RegexSetBit(bits, '\f');
	RegexSetBit(bits, '\r');
}


// The state set helpers of the matching loops. Bit pc of a set is the state of instruction pc.
constexpr bool RegexStateHas(const uint16* set, std::size_t pc) {
	return ((set[pc / 16] >> (pc % 16)) & 1u) != 0;
}


constexpr void RegexStateSet(uint16* set, std::size_t pc) {
	set[pc / 16] = static_cast<uint16>(set[pc / 16] | (1u << (pc % 16)));
}


constexpr void RegexStateClear(uint16* set, std::size_t words) {
	for (std::size_t index = 0; index < words; index++) {
		set[index] = 0;
	}
}


// True when the instruction takes the input byte. Only the consuming operations can. The match and the epsilon operations take none.
constexpr bool RegexConsumes(const RegexProgramView& program, const RegexInstruction& instruction, uint8 value) {
	if (instruction.operation == RegexOperation::character) {
		return value == instruction.x;
	}

	if (instruction.operation == RegexOperation::characterFold) {
		return value == instruction.arg || value == instruction.arg - ('a' - 'A');
	}

	if (instruction.operation == RegexOperation::any) {
		return value != '\n' && value != '\r';
	}

	if (instruction.operation == RegexOperation::classMatch) {
		return RegexHasBit(program.classes[instruction.x], value);
	}

	return false;
}


// The workspace of the plain loop. Its size is the formula of RegexPlainStackBytes.
template<std::size_t Count>
struct RegexPlainWorkspace {
	std::array<uint16, RegexStateWords(Count)> sets[2];
	std::array<uint16, Count> stack;
};


// The workspace of the captures loop, with Slots as the slot count and Frames as the closure stack size. Its size is the formula of
// RegexCapturesStackBytes.
template<std::size_t Count, std::size_t Slots, std::size_t Frames>
struct RegexCapturesWorkspace {
	std::array<uint16, Count> pcs[2];
	std::array<uint16, Count * Slots> slots[2];
	std::array<uint16, RegexStateWords(Count)> states[2];
	std::array<RegexFrame, Frames> stack;
};
