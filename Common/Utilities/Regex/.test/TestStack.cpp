#include "FrameProbe.h"
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>


// The stack sizes. Each pattern's workspace struct must have the size of its formula, and the formula is the value that Regex reports.
// The three patterns have different instruction counts: 4 with no groups, 7 with one group (odd, so the word counts round up), and 70,
// which needs three 32-bit state words per set. The closure stack of each one is counted by hand from its program: the start pc gives one
// frame, a split gives two, a save gives two, a clear gives one plus the restores for its slots, and a jump gives one.


using Plain = Regex<+[] {
	return RegexBuilder("abc");
}>;

// abc: start, then four consuming pcs, so one frame.
static_assert(Plain::PlainStackBytes == RegexPlainStackBytes(4));
static_assert(Plain::PlainStackBytes == 16);
static_assert(Plain::CaptureFrames == 1);
static_assert(sizeof(RegexPlainWorkspace<4>) == RegexPlainStackBytes(4));
static_assert(Plain::CapturesStackBytes == RegexCapturesStackBytes(4, 0, Plain::CaptureFrames));
static_assert(Plain::CapturesStackBytes == 60);
static_assert(sizeof(RegexCapturesWorkspace<4, 2, Plain::CaptureFrames>) == RegexCapturesStackBytes(4, 0, Plain::CaptureFrames));


// (a)*: start 1, split 2, clear 1 + 2 slots = 3, save 2, save 2, jump 1, so 11 frames.
using Group = Regex<+[] {
	return RegexBuilder("(a)*");
}>;

static_assert(Group::PlainStackBytes == RegexPlainStackBytes(7));
static_assert(sizeof(RegexPlainWorkspace<7>) == RegexPlainStackBytes(7));
static_assert(Group::CaptureFrames == 11);
static_assert(Group::CapturesStackBytes == RegexCapturesStackBytes(7, 1, Group::CaptureFrames));
static_assert(Group::CapturesStackBytes == 192);
static_assert(sizeof(RegexCapturesWorkspace<7, 4, Group::CaptureFrames>) == RegexCapturesStackBytes(7, 1, Group::CaptureFrames));


// a{69}: 70 instructions and no split, so one frame.
using Long = Regex<+[] {
	return RegexBuilder("a{69}");
}>;

static_assert(Long::PlainStackBytes == RegexPlainStackBytes(70));
static_assert(sizeof(RegexPlainWorkspace<70>) == RegexPlainStackBytes(70));
static_assert(Long::CaptureFrames == 1);
static_assert(Long::CapturesStackBytes == RegexCapturesStackBytes(70, 0, Long::CaptureFrames));
static_assert(Long::CapturesStackBytes == 868);
static_assert(sizeof(RegexCapturesWorkspace<70, 2, Long::CaptureFrames>) == RegexCapturesStackBytes(70, 0, Long::CaptureFrames));


// The shapes that make the most frames. Each count is worked out from the program in the comment: N is the instruction count, S the slot
// count and W the state words. The frames are the start, then each non-consuming pc's pushes, as RegexCaptureFrames adds them.
// (){4}: each copy is a clear of two slots (3 frames), and two saves (4 frames), so 1 + 4 * 7 = 29. N = 13, S = 4, W = 1.
constexpr RegexBuilder EmptyFour() {
	return RegexBuilder("(){4}");
}

using EmptyFourRegex = Regex<EmptyFour>;
static_assert(RegexCompiled<EmptyFour>::Count == 13);
static_assert(EmptyFourRegex::CaptureFrames == 29);
static_assert(EmptyFourRegex::CapturesStackBytes == 384);
static_assert(sizeof(RegexCapturesWorkspace<13, 4, 29>) == RegexCapturesStackBytes(13, 1, 29));


// (){255}: as above with 255 copies, so 1 + 255 * 7 = 1786. N = 766, S = 4, W = 24.
constexpr RegexBuilder EmptyRun() {
	return RegexBuilder("(){255}").StackLimit(32768);
}

using EmptyRunRegex = Regex<EmptyRun>;
static_assert(RegexCompiled<EmptyRun>::Count == 766);
static_assert(EmptyRunRegex::CaptureFrames == 1786);
static_assert(EmptyRunRegex::CapturesStackBytes == 22656);
static_assert(sizeof(RegexCapturesWorkspace<766, 4, 1786>) == RegexCapturesStackBytes(766, 1, 1786));


// (?:()()...()){32} with 15 empty groups in the body: the body clears 30 slots (31 frames) and has 30 saves (60 frames), so
// 1 + 32 * 91 = 2913. N = 993, S = 32, W = 32.
constexpr RegexBuilder EmptyGroups() {
	return RegexBuilder("(?:()()()()()()()()()()()()()()()){32}").StackLimit(147456);
}

using EmptyGroupsRegex = Regex<EmptyGroups>;
static_assert(RegexCompiled<EmptyGroups>::Count == 993);
static_assert(RegexCompiled<EmptyGroups>::GroupCount == 15);
static_assert(EmptyGroupsRegex::CaptureFrames == 2913);
static_assert(EmptyGroupsRegex::CapturesStackBytes == 142984);
static_assert(sizeof(RegexCapturesWorkspace<993, 32, 2913>) == RegexCapturesStackBytes(993, 15, 2913));


// (?:(a?)...(a?))* with 15 groups: the loop split 2, the body clear 31, each group 6 (save 2, split 2, save 2), the jump 1, so
// 1 + 2 + 31 + 90 + 1 = 125. N = 64, S = 32, W = 2.
constexpr RegexBuilder OptionalGroups() {
	return RegexBuilder("(?:(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?)(a?))*").StackLimit(16384);
}

using OptionalGroupsRegex = Regex<OptionalGroups>;
static_assert(RegexCompiled<OptionalGroups>::Count == 64);
static_assert(RegexCompiled<OptionalGroups>::GroupCount == 15);
static_assert(OptionalGroupsRegex::CaptureFrames == 125);
static_assert(OptionalGroupsRegex::CapturesStackBytes == 8964);
static_assert(sizeof(RegexCapturesWorkspace<64, 32, 125>) == RegexCapturesStackBytes(64, 15, 125));


// The formulas against the values of the design's Memory examples. Each takes a closure stack of 2N frames, the count the first design
// used. These are checks of the formulas only, the frames of a real pattern come from its program.
static_assert(RegexPlainStackBytes(64) == 144);
static_assert(RegexCapturesStackBytes(64, 4, 2 * 64) == 3344);
static_assert(RegexCapturesStackBytes(40, 4, 2 * 40) == 2096);
static_assert(RegexPlainStackBytes(1024) == 2304);
static_assert(RegexCapturesStackBytes(1024, 4, 2 * 1024) == 53504);


// Runs Find on the text, and checks that the closure stack never went above CaptureFrames. The probe must see at least one push, or the
// hook is not wired and the check means nothing.
template<RegexBuilder (*Make)()>
void ExpectWithin(std::string_view text, int line, int& failures) {
	using Pattern = Regex<Make>;
	RegexProbe::Reset();
	static_cast<void>(Pattern::Find(text));
	if (RegexProbe::highWater == 0) {
		failures++;
		std::printf("FAIL line %d: the probe saw no push\n", line);
	}

	if (RegexProbe::highWater > Pattern::CaptureFrames) {
		failures++;
		std::printf("FAIL line %d: the closure stack reached %zu frames, CaptureFrames is %zu\n", line, RegexProbe::highWater, Pattern::CaptureFrames);
	}
}


int main() {
	int failures = 0;
	std::array<char, 100> run{};
	run.fill('a');
	std::string_view runs(run.data(), run.size());
	ExpectWithin<EmptyFour>("", __LINE__, failures);
	ExpectWithin<EmptyFour>("aaaa", __LINE__, failures);
	ExpectWithin<EmptyRun>("", __LINE__, failures);
	ExpectWithin<EmptyRun>("aa", __LINE__, failures);
	ExpectWithin<EmptyGroups>("", __LINE__, failures);
	ExpectWithin<EmptyGroups>("aaaa", __LINE__, failures);
	ExpectWithin<OptionalGroups>("", __LINE__, failures);
	ExpectWithin<OptionalGroups>("a", __LINE__, failures);
	ExpectWithin<OptionalGroups>("aaaaaaaaaaaaaaaa", __LINE__, failures);
	ExpectWithin<OptionalGroups>(runs, __LINE__, failures);
	std::printf("stack shapes: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
