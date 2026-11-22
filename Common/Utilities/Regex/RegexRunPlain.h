#pragma once
#include "RegexProgram.h"
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <string_view>


// The arrays of the plain loop, as the caller holds them: two state sets and the closure stack.
struct RegexPlainWork {
	uint16* current;
	uint16* next;
	uint16* stack;
};


// Adds one pc to the closure being built, unless it is already in the set.
inline void RegexPushPlain(uint16* set, uint16* stack, std::size_t& top, std::size_t pc) {
	if (RegexStateHas(set, pc)) {
		return;
	}

	RegexStateSet(set, pc);
	stack[top] = static_cast<uint16>(pc);
	top++;
}


// Adds the closure of start at offset to the set. Every pc enters the stack at most once, so N entries are enough. The epsilon operations
// are followed here, and the consuming pcs and the match stay in the set.
inline void RegexAddPlainThread(
	const RegexProgramView& program,
	uint16* set,
	uint16* stack,
	std::size_t start,
	std::size_t offset,
	std::size_t size)
{
	std::size_t top = 0;
	RegexPushPlain(set, stack, top, start);
	while (top > 0) {
		top--;
		std::size_t pc = stack[top];
		const RegexInstruction& instruction = program.code[pc];
		if (instruction.operation == RegexOperation::jump) {
			RegexPushPlain(set, stack, top, instruction.x);
		} else if (instruction.operation == RegexOperation::split) {
			RegexPushPlain(set, stack, top, instruction.y);
			RegexPushPlain(set, stack, top, instruction.x);
		} else if (instruction.operation == RegexOperation::begin) {
			if (offset == 0) {
				RegexPushPlain(set, stack, top, pc + 1);
			}
		} else if (instruction.operation == RegexOperation::end) {
			if (offset == size) {
				RegexPushPlain(set, stack, top, pc + 1);
			}
		} else if (instruction.operation == RegexOperation::save || instruction.operation == RegexOperation::clear) {
			RegexPushPlain(set, stack, top, pc + 1);
		}
	}
}


// The plain loop, shared by all patterns. With whole set, the whole text must match, as std::regex_match does. Otherwise the answer is
// whether some part of the text matches, as std::regex_search does. The state sets hold the closure at the current offset.
inline bool RegexRunPlain(const RegexProgramView& program, const RegexPlainWork& work, std::string_view text, bool whole) {
	const std::size_t size = text.size();
	const std::size_t words = RegexStateWords(program.count);
	const std::size_t matchPc = program.count - 1;
	uint16* current = work.current;
	uint16* next = work.next;
	RegexStateClear(current, words);
	if (whole) {
		RegexAddPlainThread(program, current, work.stack, 0, 0, size);
	}

	for (std::size_t offset = 0; ; offset++) {
		if (!whole) {
			RegexAddPlainThread(program, current, work.stack, 0, offset, size);
			if (RegexStateHas(current, matchPc)) {
				return true;
			}
		}

		if (offset == size) {
			return whole && RegexStateHas(current, matchPc);
		}

		RegexStateClear(next, words);
		uint8 value = static_cast<uint8>(text[offset]);
		for (std::size_t pc = 0; pc < program.count; pc++) {
			if (RegexStateHas(current, pc) && RegexConsumes(program, program.code[pc], value)) {
				RegexAddPlainThread(program, next, work.stack, pc + 1, offset + 1, size);
			}
		}

		uint16* temp = current;
		current = next;
		next = temp;
	}
}
