#include <Utilities/Regex/RegexBuilder.h>
#include <cstddef>
#include <cstdio>


// The shapes of programs for a few patterns, with the instruction counts and operands worked out by hand from the rules of the design.
// A failing static_assert names the pattern. The program of each pattern is read through its View.


constexpr bool Same(const RegexInstruction& instruction, RegexOperation operation, uint8 arg, uint16 x, uint16 y) {
	return instruction.operation == operation && instruction.arg == arg && instruction.x == x && instruction.y == y;
}


using EmptyProgram = RegexCompiled<+[] {
	return RegexBuilder("");
}>;

static_assert(EmptyProgram::Count == 1);
static_assert(EmptyProgram::ClassCount == 0);
static_assert(EmptyProgram::GroupCount == 0);
static_assert(EmptyProgram::SlotCount == 2);
static_assert(EmptyProgram::View.count == 1);
static_assert(EmptyProgram::View.slotCount == 2);
static_assert(Same(EmptyProgram::View.code[0], RegexOperation::match, 0, 0, 0));


using Literal = RegexCompiled<+[] {
	return RegexBuilder("abc");
}>;

static_assert(Literal::Count == 4);
static_assert(Literal::ClassCount == 0);
static_assert(Same(Literal::View.code[0], RegexOperation::character, 0, 'a', 0));
static_assert(Same(Literal::View.code[1], RegexOperation::character, 0, 'b', 0));
static_assert(Same(Literal::View.code[2], RegexOperation::character, 0, 'c', 0));
static_assert(Same(Literal::View.code[3], RegexOperation::match, 0, 0, 0));


using PlusClass = RegexCompiled<+[] {
	return RegexBuilder("[a-z]+");
}>;

static_assert(PlusClass::Count == 3);
static_assert(PlusClass::ClassCount == 1);
static_assert(Same(PlusClass::View.code[0], RegexOperation::classMatch, 0, 0, 0));
static_assert(Same(PlusClass::View.code[1], RegexOperation::split, 0, 0, 2));
static_assert(Same(PlusClass::View.code[2], RegexOperation::match, 0, 0, 0));
static_assert(RegexHasBit(PlusClass::View.classes[0], 'a'));
static_assert(RegexHasBit(PlusClass::View.classes[0], 'z'));
static_assert(!RegexHasBit(PlusClass::View.classes[0], 'A'));
static_assert(!RegexHasBit(PlusClass::View.classes[0], '{'));


using SharedClass = RegexCompiled<+[] {
	return RegexBuilder("[a-z][a-z]");
}>;

static_assert(SharedClass::Count == 3);
static_assert(SharedClass::ClassCount == 1);
static_assert(SharedClass::View.code[0].x == 0);
static_assert(SharedClass::View.code[1].x == 0);


using TwoClasses = RegexCompiled<+[] {
	return RegexBuilder("[0-9][0-9][a-f]");
}>;

static_assert(TwoClasses::Count == 4);
static_assert(TwoClasses::ClassCount == 2);
static_assert(TwoClasses::View.code[0].x == 0);
static_assert(TwoClasses::View.code[1].x == 0);
static_assert(TwoClasses::View.code[2].x == 1);


using DigitAndClass = RegexCompiled<+[] {
	return RegexBuilder(R"([a-z]\d)");
}>;

static_assert(DigitAndClass::Count == 3);
static_assert(DigitAndClass::ClassCount == 2);
static_assert(DigitAndClass::View.code[0].x == 0);
static_assert(DigitAndClass::View.code[1].x == 1);
static_assert(RegexHasBit(DigitAndClass::View.classes[1], '7'));
static_assert(!RegexHasBit(DigitAndClass::View.classes[1], 'a'));


using ExactRepeat = RegexCompiled<+[] {
	return RegexBuilder("a{3}");
}>;

static_assert(ExactRepeat::Count == 4);
static_assert(Same(ExactRepeat::View.code[0], RegexOperation::character, 0, 'a', 0));
static_assert(Same(ExactRepeat::View.code[1], RegexOperation::character, 0, 'a', 0));
static_assert(Same(ExactRepeat::View.code[2], RegexOperation::character, 0, 'a', 0));
static_assert(Same(ExactRepeat::View.code[3], RegexOperation::match, 0, 0, 0));


using BoundedRepeat = RegexCompiled<+[] {
	return RegexBuilder("a{2,4}");
}>;

static_assert(BoundedRepeat::Count == 7);
static_assert(Same(BoundedRepeat::View.code[2], RegexOperation::split, 0, 3, 6));
static_assert(Same(BoundedRepeat::View.code[4], RegexOperation::split, 0, 5, 6));


using OpenRepeat = RegexCompiled<+[] {
	return RegexBuilder("a{2,}");
}>;

static_assert(OpenRepeat::Count == 4);
static_assert(Same(OpenRepeat::View.code[2], RegexOperation::split, 0, 1, 3));


using StarGreedy = RegexCompiled<+[] {
	return RegexBuilder("a*");
}>;

static_assert(StarGreedy::Count == 4);
static_assert(Same(StarGreedy::View.code[0], RegexOperation::split, 0, 1, 3));
static_assert(Same(StarGreedy::View.code[1], RegexOperation::character, 0, 'a', 0));
static_assert(Same(StarGreedy::View.code[2], RegexOperation::jump, 0, 0, 0));


using StarLazy = RegexCompiled<+[] {
	return RegexBuilder("a*?");
}>;

static_assert(StarLazy::Count == 4);
static_assert(Same(StarLazy::View.code[0], RegexOperation::split, 0, 3, 1));


using PlusOnce = RegexCompiled<+[] {
	return RegexBuilder("a+");
}>;

static_assert(PlusOnce::Count == 3);
static_assert(Same(PlusOnce::View.code[1], RegexOperation::split, 0, 0, 2));


using OptionalLazy = RegexCompiled<+[] {
	return RegexBuilder("a??");
}>;

static_assert(OptionalLazy::Count == 3);
static_assert(Same(OptionalLazy::View.code[0], RegexOperation::split, 0, 2, 1));


using Optional = RegexCompiled<+[] {
	return RegexBuilder("a?");
}>;

static_assert(Optional::Count == 3);
static_assert(Same(Optional::View.code[0], RegexOperation::split, 0, 1, 2));


using TwoOptional = RegexCompiled<+[] {
	return RegexBuilder("a{0,2}");
}>;

static_assert(TwoOptional::Count == 5);
static_assert(Same(TwoOptional::View.code[0], RegexOperation::split, 0, 1, 4));
static_assert(Same(TwoOptional::View.code[2], RegexOperation::split, 0, 3, 4));


using ZeroRepeat = RegexCompiled<+[] {
	return RegexBuilder("a{0}");
}>;

static_assert(ZeroRepeat::Count == 1);


using BigRepeat = RegexCompiled<+[] {
	return RegexBuilder("a{255}");
}>;

static_assert(BigRepeat::Count == 256);


using Group = RegexCompiled<+[] {
	return RegexBuilder("(a)");
}>;

static_assert(Group::Count == 4);
static_assert(Group::GroupCount == 1);
static_assert(Group::SlotCount == 4);
static_assert(Same(Group::View.code[0], RegexOperation::save, 2, 0, 0));
static_assert(Same(Group::View.code[1], RegexOperation::character, 0, 'a', 0));
static_assert(Same(Group::View.code[2], RegexOperation::save, 3, 0, 0));


using GroupStar = RegexCompiled<+[] {
	return RegexBuilder("(a)*");
}>;

// The loop: split, then the body with its clear of slots 2 to 4, then the jump back.
static_assert(GroupStar::Count == 7);
static_assert(Same(GroupStar::View.code[0], RegexOperation::split, 0, 1, 6));
static_assert(Same(GroupStar::View.code[1], RegexOperation::clear, 2, 4, 0));
static_assert(Same(GroupStar::View.code[2], RegexOperation::save, 2, 0, 0));
static_assert(Same(GroupStar::View.code[4], RegexOperation::save, 3, 0, 0));
static_assert(Same(GroupStar::View.code[5], RegexOperation::jump, 0, 0, 0));


using TwoGroups = RegexCompiled<+[] {
	return RegexBuilder("(a)(b)");
}>;

static_assert(TwoGroups::Count == 7);
static_assert(TwoGroups::GroupCount == 2);
static_assert(TwoGroups::SlotCount == 6);
static_assert(Same(TwoGroups::View.code[3], RegexOperation::save, 4, 0, 0));


using EmptyGroup = RegexCompiled<+[] {
	return RegexBuilder("()");
}>;

static_assert(EmptyGroup::Count == 3);
static_assert(Same(EmptyGroup::View.code[0], RegexOperation::save, 2, 0, 0));
static_assert(Same(EmptyGroup::View.code[1], RegexOperation::save, 3, 0, 0));


using NonCapturing = RegexCompiled<+[] {
	return RegexBuilder("(?:ab)+");
}>;

static_assert(NonCapturing::Count == 4);
static_assert(NonCapturing::GroupCount == 0);
static_assert(Same(NonCapturing::View.code[2], RegexOperation::split, 0, 0, 3));


using Alternation = RegexCompiled<+[] {
	return RegexBuilder("a|b");
}>;

static_assert(Alternation::Count == 5);
static_assert(Same(Alternation::View.code[0], RegexOperation::split, 0, 1, 3));
static_assert(Same(Alternation::View.code[1], RegexOperation::character, 0, 'a', 0));
static_assert(Same(Alternation::View.code[2], RegexOperation::jump, 0, 4, 0));
static_assert(Same(Alternation::View.code[3], RegexOperation::character, 0, 'b', 0));


using EmptyAlternative = RegexCompiled<+[] {
	return RegexBuilder("a|");
}>;

static_assert(EmptyAlternative::Count == 4);
static_assert(Same(EmptyAlternative::View.code[0], RegexOperation::split, 0, 1, 3));
static_assert(Same(EmptyAlternative::View.code[2], RegexOperation::jump, 0, 3, 0));


using Anchors = RegexCompiled<+[] {
	return RegexBuilder("^$");
}>;

static_assert(Anchors::Count == 3);
static_assert(Same(Anchors::View.code[0], RegexOperation::begin, 0, 0, 0));
static_assert(Same(Anchors::View.code[1], RegexOperation::end, 0, 0, 0));


using HexEscape = RegexCompiled<+[] {
	return RegexBuilder(R"(\x41)");
}>;

static_assert(HexEscape::Count == 2);
static_assert(Same(HexEscape::View.code[0], RegexOperation::character, 0, 65, 0));


using EscapedBar = RegexCompiled<+[] {
	return RegexBuilder(R"(a\|b)");
}>;

static_assert(EscapedBar::Count == 4);
static_assert(Same(EscapedBar::View.code[1], RegexOperation::character, 0, '|', 0));


using FoldLetters = RegexCompiled<+[] {
	return RegexBuilder("Ab").IgnoreCase();
}>;

static_assert(FoldLetters::Count == 3);
static_assert(FoldLetters::IgnoreCase);
static_assert(Same(FoldLetters::View.code[0], RegexOperation::characterFold, 'a', 0, 0));
static_assert(Same(FoldLetters::View.code[1], RegexOperation::characterFold, 'b', 0, 0));


using FoldDigit = RegexCompiled<+[] {
	return RegexBuilder("7").IgnoreCase();
}>;

static_assert(Same(FoldDigit::View.code[0], RegexOperation::character, 0, '7', 0));


using FoldClass = RegexCompiled<+[] {
	return RegexBuilder("[a]").IgnoreCase();
}>;

static_assert(RegexHasBit(FoldClass::View.classes[0], 'a'));
static_assert(RegexHasBit(FoldClass::View.classes[0], 'A'));
static_assert(!RegexHasBit(FoldClass::View.classes[0], 'b'));


using FoldNegated = RegexCompiled<+[] {
	return RegexBuilder("[^a]").IgnoreCase();
}>;

// Fold first, then negate: both cases of a are excluded.
static_assert(!RegexHasBit(FoldNegated::View.classes[0], 'a'));
static_assert(!RegexHasBit(FoldNegated::View.classes[0], 'A'));
static_assert(RegexHasBit(FoldNegated::View.classes[0], 'b'));
static_assert(RegexHasBit(FoldNegated::View.classes[0], 'B'));


using ClassEdges = RegexCompiled<+[] {
	return RegexBuilder(R"([-\]a])");
}>;

// A leading dash is a literal, an escaped bracket is a byte, and the class is { '-', ']', 'a' }.
static_assert(ClassEdges::ClassCount == 1);
static_assert(RegexHasBit(ClassEdges::View.classes[0], ']'));
static_assert(RegexHasBit(ClassEdges::View.classes[0], '-'));
static_assert(RegexHasBit(ClassEdges::View.classes[0], 'a'));
static_assert(!RegexHasBit(ClassEdges::View.classes[0], 'b'));
static_assert(!RegexHasBit(ClassEdges::View.classes[0], '_'));


using NegatedShorthand = RegexCompiled<+[] {
	return RegexBuilder(R"(\W)");
}>;

static_assert(NegatedShorthand::ClassCount == 1);
static_assert(!RegexHasBit(NegatedShorthand::View.classes[0], 'a'));
static_assert(!RegexHasBit(NegatedShorthand::View.classes[0], '_'));
static_assert(RegexHasBit(NegatedShorthand::View.classes[0], ' '));
static_assert(RegexHasBit(NegatedShorthand::View.classes[0], '!'));


using Stacks = RegexCompiled<+[] {
	return RegexBuilder("abc").StackLimit(2000);
}>;

static_assert(Stacks::Count == 4);
static_assert(Stacks::PlainStackBytes == 16);
static_assert(Stacks::CaptureFrames == 1);
static_assert(Stacks::CapturesStackBytes == 60);
static_assert(Stacks::StackLimit == 2000);
static_assert(Stacks::View.slotCount == 2);


// The formulas, against the values of the design's Memory examples. The closure stack is given as 2N frames, the count the first design
// used, so these check the arithmetic only.
static_assert(RegexPlainStackBytes(64) == 144);
static_assert(RegexCapturesStackBytes(64, 4, 2 * 64) == 3344);
static_assert(RegexCapturesStackBytes(40, 4, 2 * 40) == 2096);
static_assert(RegexPlainStackBytes(1024) == 2304);
static_assert(RegexCapturesStackBytes(1024, 4, 2 * 1024) == 53504);


int main() {
	std::puts("program shapes ok");
	return 0;
}
