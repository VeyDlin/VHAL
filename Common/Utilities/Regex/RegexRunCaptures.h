#pragma once
#include "RegexProgram.h"
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <string_view>


// One thread list of the captures loop, in priority order. Entry k is the pc pcs[k] with the slot values at slots[k * SlotCount]. The
// state set marks every pc that the closure visited, consuming or not. The entry after the last one holds the working slots of the path
// that is being explored.
struct RegexThreadList {
	uint16* pcs;
	uint16* slots;
	uint16* states;
	std::size_t count;
};


// The arrays of the captures loop, as the caller holds them: two thread lists and the closure stack.
struct RegexCapturesWork {
	uint16* pcs[2];
	uint16* slots[2];
	uint16* states[2];
	RegexFrame* stack;
};


// The working slots: the slots of the entry after the last one. A list with all its pcs has no working slots, and the callers do not use them then.
inline uint16* RegexWorkingSlots(const RegexThreadList& list, std::size_t slotCount) {
	return list.slots + list.count * slotCount;
}


// A test may define REGEX_TRACE_FRAMES(depth) before this header. It then sees the depth of the closure stack after each push. Release
// builds leave it empty, so the code they compile has no hook.
#ifndef REGEX_TRACE_FRAMES
#define REGEX_TRACE_FRAMES(depth)
#endif


inline void RegexPushFrame(RegexFrame* stack, std::size_t& top, uint16 key, uint16 value) {
	stack[top].key = key;
	stack[top].value = value;
	top++;
	REGEX_TRACE_FRAMES(top);
}


inline void RegexPushPc(RegexFrame* stack, std::size_t& top, std::size_t pc) {
	RegexPushFrame(stack, top, static_cast<uint16>(pc), 0);
}


// Adds the closure of start at offset to the list. The working slots first take the values of from, when from is not null. Each pc is
// visited once, and it is checked when it is popped, so the first path to reach a pc in priority order keeps it. A consuming pc or the match
// becomes an entry, and the working slots become its values. Then a copy of them becomes the new working slots.
//
// The closure stack holds the pending siblings of the splits, and a restore frame for each slot that a save or a clear changed. A pc is
// checked when it is popped, so each pc is processed once per closure. The frames pending at any time are at most the frames that the
// closure pushes, and RegexCaptureFrames bounds those over the whole program. The workspace has that many frames.
inline void RegexAddCaptureThread(
	const RegexProgramView& program,
	RegexThreadList& list,
	RegexFrame* stack,
	std::size_t start,
	std::size_t offset,
	std::size_t size,
	const uint16* from)
{
	const std::size_t slotCount = program.slotCount;
	if (list.count >= program.count) {
		return;
	}

	if (from != nullptr) {
		uint16* seed = RegexWorkingSlots(list, slotCount);
		for (std::size_t slot = 0; slot < slotCount; slot++) {
			seed[slot] = from[slot];
		}
	}

	std::size_t top = 0;
	RegexPushPc(stack, top, start);
	while (top > 0) {
		top--;
		RegexFrame frame = stack[top];
		if ((frame.key & RegexRestoreFlag) != 0) {
			uint16* restored = RegexWorkingSlots(list, slotCount);
			restored[frame.key & RegexSlotMask] = frame.value;
			continue;
		}

		std::size_t pc = frame.key;
		if (RegexStateHas(list.states, pc)) {
			continue;
		}

		RegexStateSet(list.states, pc);
		uint16* working = RegexWorkingSlots(list, slotCount);
		const RegexInstruction& instruction = program.code[pc];
		if (instruction.operation == RegexOperation::jump) {
			RegexPushPc(stack, top, instruction.x);
		} else if (instruction.operation == RegexOperation::split) {
			RegexPushPc(stack, top, instruction.y);
			RegexPushPc(stack, top, instruction.x);
		} else if (instruction.operation == RegexOperation::begin) {
			if (offset == 0) {
				RegexPushPc(stack, top, pc + 1);
			}
		} else if (instruction.operation == RegexOperation::end) {
			if (offset == size) {
				RegexPushPc(stack, top, pc + 1);
			}
		} else if (instruction.operation == RegexOperation::save) {
			RegexPushFrame(stack, top, static_cast<uint16>(RegexRestoreFlag | instruction.arg), working[instruction.arg]);
			working[instruction.arg] = static_cast<uint16>(offset);
			RegexPushPc(stack, top, pc + 1);
		} else if (instruction.operation == RegexOperation::clear) {
			for (std::size_t slot = instruction.arg; slot < instruction.x; slot++) {
				if (working[slot] != RegexSlotUnset) {
					RegexPushFrame(stack, top, static_cast<uint16>(RegexRestoreFlag | slot), working[slot]);
					working[slot] = RegexSlotUnset;
				}
			}

			RegexPushPc(stack, top, pc + 1);
		} else {
			// A consuming pc or the match. Its entry keeps the working slots, and the next working slots start as a copy of them.
			list.pcs[list.count] = static_cast<uint16>(pc);
			list.count++;
			if (list.count < program.count) {
				uint16* fresh = RegexWorkingSlots(list, slotCount);
				for (std::size_t slot = 0; slot < slotCount; slot++) {
					fresh[slot] = working[slot];
				}
			}
		}
	}
}


// Adds a thread that starts at offset, with the lowest priority in the list. Its slot 0 is the offset and the other slots are unset.
inline void RegexStartCaptureThread(const RegexProgramView& program, RegexThreadList& list, RegexFrame* stack, std::size_t offset, std::size_t size) {
	if (list.count >= program.count) {
		return;
	}

	uint16* working = RegexWorkingSlots(list, program.slotCount);
	working[0] = static_cast<uint16>(offset);
	for (std::size_t slot = 1; slot < program.slotCount; slot++) {
		working[slot] = RegexSlotUnset;
	}

	RegexAddCaptureThread(program, list, stack, 0, offset, size, nullptr);
}


// The captures loop, shared by all patterns. It searches from offset from, and it is leftmost-first: when a thread reaches the match, the
// lower-priority threads are dropped, and the higher-priority ones may still replace the result. The slots of the match go to best, with
// slot 0 at the start and slot 1 at the end. The result is false when nothing matches.
inline bool RegexRunCaptures(
	const RegexProgramView& program,
	const RegexCapturesWork& work,
	std::string_view text,
	std::size_t from,
	uint16* best)
{
	const std::size_t size = text.size();
	const std::size_t slotCount = program.slotCount;
	const std::size_t words = RegexStateWords(program.count);
	RegexThreadList current{work.pcs[0], work.slots[0], work.states[0], 0};
	RegexThreadList next{work.pcs[1], work.slots[1], work.states[1], 0};
	RegexStateClear(current.states, words);
	bool found = false;
	for (std::size_t offset = from; ; offset++) {
		if (!found) {
			RegexStartCaptureThread(program, current, work.stack, offset, size);
		}

		if (found && current.count == 0) {
			break;
		}

		next.count = 0;
		RegexStateClear(next.states, words);
		for (std::size_t index = 0; index < current.count; index++) {
			std::size_t pc = current.pcs[index];
			const RegexInstruction& instruction = program.code[pc];
			if (instruction.operation == RegexOperation::match) {
				for (std::size_t slot = 0; slot < slotCount; slot++) {
					best[slot] = current.slots[index * slotCount + slot];
				}

				best[1] = static_cast<uint16>(offset);
				found = true;
				break;
			}

			if (offset < size && RegexConsumes(program, instruction, static_cast<uint8>(text[offset]))) {
				RegexAddCaptureThread(program, next, work.stack, pc + 1, offset + 1, size, current.slots + index * slotCount);
			}
		}

		if (offset >= size) {
			break;
		}

		RegexThreadList temp = current;
		current = next;
		next = temp;
	}

	return found;
}
