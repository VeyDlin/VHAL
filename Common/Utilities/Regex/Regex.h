#pragma once
#include "RegexBuilder.h"
#include "RegexError.h"
#include "RegexProgram.h"
#include "RegexRunCaptures.h"
#include "RegexRunPlain.h"
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <string_view>


// The longest input that Find takes. Capture slots are uint16 offsets, and 0xFFFF means unset, so the last offset is 65534.
constexpr std::size_t RegexMaxFindInput = 65534;


// A compiled pattern with its matching functions. The pattern is a captureless lambda that returns a RegexBuilder, as in RegexBuilder.h:
//
//   using Url = Regex<+[] {
//       return RegexBuilder(R"(^(https?)://([a-z0-9.-]+)(?::([0-9]{1,5}))?(/[^ ]*)?$)").IgnoreCase();
//   }>;
//
//   bool ok = Url::Matches(text);
//   Result<Url::Captures> found = Url::Find(text);
//
// Matches and Contains run the plain loop only. Find runs the captures loop, so a program that never calls Find has no capture code.
//
// Stack. Each member function checks the workspace of its own loop against StackLimit, with a static_assert through the Require helpers.
// The workspace is a local of the function, so the caller pays for the workspace of the functions it calls only.
//
// Closure stack of the captures loop. The loop checks a pc when it pops it, so each pc is processed once per closure. Processing a pc
// pushes a few frames, and the frames pending at any time are never more than the frames pushed in one closure. RegexCaptureFrames adds
// up those pushes over the program, and the workspace of Find holds exactly that many frames.
template<RegexBuilder (*Make)()>
class Regex {
private:
	using Program = RegexCompiled<Make>;


public:
	// The stack bytes of the two loops, as the formulas of RegexProgram.h give them for this pattern.
	static constexpr std::size_t PlainStackBytes = Program::PlainStackBytes;
	static constexpr std::size_t CapturesStackBytes = Program::CapturesStackBytes;
	// The closure stack of the captures loop, in frames of 4 bytes each.
	static constexpr std::size_t CaptureFrames = Program::CaptureFrames;
	static constexpr std::size_t GroupCount = Program::GroupCount;


	// The result of Find: the offsets of group 0 to GroupCount, and a bit for each group that matched.
	class Captures {
	private:
		std::string_view input{};
		std::array<uint16, Program::SlotCount> slots{};
		uint16 mask = 0;

		friend class Regex;


	public:
		// The offsets of the whole match in the input.
		std::size_t Begin() const {
			return slots[0];
		}


		std::size_t End() const {
			return slots[1];
		}


		// The text of group Index. An index above the group count is a compile error. A group that did not match gives an empty view.
		template<std::size_t Index>
		std::string_view Group() const {
			RegexRequireGroup<Index, Program::GroupCount>();
			return Part(Index);
		}


		// The text of group index at run time. An index above the group count, or a group that did not match, gives an empty view.
		std::string_view Group(std::size_t index) const {
			if (index > Program::GroupCount) {
				return std::string_view();
			}

			return Part(index);
		}


		// True when group index took part in the match.
		bool Matched(std::size_t index) const {
			return index <= Program::GroupCount && ((mask >> index) & 1u) != 0;
		}


	private:
		std::string_view Part(std::size_t index) const {
			if (!Matched(index)) {
				return std::string_view();
			}

			std::size_t start = slots[2 * index];
			std::size_t end = slots[2 * index + 1];
			return input.substr(start, end - start);
		}
	};


	// True when the whole text matches the pattern, as std::regex_match does.
	static bool Matches(std::string_view text) {
		static constexpr bool checked = (RegexRequirePlainStack<PlainStackBytes, Program::StackLimit>(), true);
		static_cast<void>(checked);
		RegexPlainWorkspace<Program::Count> work;
		RegexPlainWork view{work.sets[0].data(), work.sets[1].data(), work.stack.data()};
		return RegexRunPlain(Program::View, view, text, true);
	}


	// True when some part of the text matches the pattern, as std::regex_search does. For a text up to RegexMaxFindInput bytes, the same answer
// as Find(text).IsOk().
	static bool Contains(std::string_view text) {
		static constexpr bool checked = (RegexRequirePlainStack<PlainStackBytes, Program::StackLimit>(), true);
		static_cast<void>(checked);
		RegexPlainWorkspace<Program::Count> work;
		RegexPlainWork view{work.sets[0].data(), work.sets[1].data(), work.stack.data()};
		return RegexRunPlain(Program::View, view, text, false);
	}


	// The leftmost-first match that starts at offset from or later. The result is noMatches when nothing matches, invalidParameter when
	// from is past the end of the text, and outOfRange when the text is longer than RegexMaxFindInput. The start of the text stays at
	// offset 0, so ^ still means the start of the whole text.
	static Result<Captures> Find(std::string_view text, std::size_t from = 0) {
		static constexpr bool checked = (RegexRequireCapturesStack<CapturesStackBytes, Program::StackLimit>(), true);
		static_cast<void>(checked);
		if (from > text.size()) {
			return Err<ResultStatus>(ResultStatus::invalidParameter);
		}

		if (text.size() > RegexMaxFindInput) {
			return Err<ResultStatus>(ResultStatus::outOfRange);
		}

		RegexCapturesWorkspace<Program::Count, Program::SlotCount, Program::CaptureFrames> work;
		RegexCapturesWork view{
			{work.pcs[0].data(), work.pcs[1].data()},
			{work.slots[0].data(), work.slots[1].data()},
			{work.states[0].data(), work.states[1].data()},
			work.stack.data()
		};
		Captures captures;
		captures.input = text;
		if (!RegexRunCaptures(Program::View, view, text, from, captures.slots.data())) {
			return Err<ResultStatus>(ResultStatus::noMatches);
		}

		for (std::size_t group = 0; group <= GroupCount; group++) {
			if (captures.slots[2 * group] != RegexSlotUnset && captures.slots[2 * group + 1] != RegexSlotUnset) {
				captures.mask = static_cast<uint16>(captures.mask | (1u << group));
			}
		}

		return Ok<Captures>(captures);
	}
};
