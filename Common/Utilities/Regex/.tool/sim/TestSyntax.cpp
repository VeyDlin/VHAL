#include "FrameProbe.h"
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <cstdio>
#include <string_view>


// The syntax table of SyntaxCases.inc, with the expected results written by hand (not from std::regex and not from this code). Each case
// checks Matches, Contains, Find and the text of the leftmost-first match. The capture checks after the table cover group numbering,
// the reset of groups inside a repeat, and the Begin and End offsets.


namespace {


int checks = 0;
int failures = 0;
int caseCount = 0;


void Expect(int line, const char* what, bool ok) {
	checks++;
	if (!ok) {
		failures++;
		std::printf("FAIL line %d: %s\n", line, what);
	}
}


template<RegexBuilder (*Make)()>
void CheckCase(int line, std::string_view text, bool matches, bool contains, bool found, std::string_view whole) {
	using Pattern = Regex<Make>;
	caseCount++;
	bool gotMatches = Pattern::Matches(text);
	bool gotContains = Pattern::Contains(text);
	Expect(line, "Matches", gotMatches == matches);
	Expect(line, "Contains", gotContains == contains);
	Expect(line, "Matches implies Contains", !gotMatches || gotContains);
	RegexProbe::Reset();
	Result<typename Pattern::Captures> result = Pattern::Find(text);
	Expect(line, "closure stack within CaptureFrames", RegexProbe::highWater <= Pattern::CaptureFrames);
	Expect(line, "Contains is the same as Find", gotContains == result.IsOk());
	if (found) {
		Expect(line, "Find finds a match", result.IsOk());
		if (result.IsOk()) {
			Expect(line, "Find text of group 0", result.Value().Group(0) == whole);
		}
	} else {
		Expect(line, "Find returns noMatches", !result.IsOk() && result.Error() == ResultStatus::noMatches);
	}
}


void CaptureChecks() {
	using Alternate = Regex<+[] {
		return RegexBuilder("(a)|(b)");
	}>;
	Result<Alternate::Captures> alternate = Alternate::Find("b");
	Expect(__LINE__, "(a)|(b) finds b", alternate.IsOk());
	if (alternate.IsOk()) {
		Expect(__LINE__, "(a)|(b) group 1 is unset", !alternate.Value().Matched(1) && alternate.Value().Group<1>().empty());
		Expect(__LINE__, "(a)|(b) group 2 is b", alternate.Value().Matched(2) && alternate.Value().Group<2>() == "b");
		Expect(__LINE__, "(a)|(b) offsets", alternate.Value().Begin() == 0 && alternate.Value().End() == 1);
	}

	// A group inside a repeat is reset at each iteration: group 2 is unset after the last b.
	using Reset = Regex<+[] {
		return RegexBuilder("((a)|b)+");
	}>;
	Result<Reset::Captures> reset = Reset::Find("ab");
	Expect(__LINE__, "((a)|b)+ finds ab", reset.IsOk());
	if (reset.IsOk()) {
		Expect(__LINE__, "((a)|b)+ group 1 is b", reset.Value().Group<1>() == "b");
		Expect(__LINE__, "((a)|b)+ group 2 is unset", !reset.Value().Matched(2));
	}

	using Last = Regex<+[] {
		return RegexBuilder("(a)*");
	}>;
	Result<Last::Captures> last = Last::Find("aa");
	Expect(__LINE__, "(a)* finds aa", last.IsOk() && last.Value().Group<0>() == "aa");
	Expect(__LINE__, "(a)* group 1 is the last iteration", last.IsOk() && last.Value().Group<1>() == "a");

	using Skipped = Regex<+[] {
		return RegexBuilder("(?:(a)|b)*");
	}>;
	Result<Skipped::Captures> skipped = Skipped::Find("ab");
	Expect(__LINE__, "(?:(a)|b)* group 0 is ab", skipped.IsOk() && skipped.Value().Group(0) == "ab");
	Expect(__LINE__, "(?:(a)|b)* group 1 is unset after b", skipped.IsOk() && !skipped.Value().Matched(1));

	using Optional = Regex<+[] {
		return RegexBuilder("x(y)?z");
	}>;
	Result<Optional::Captures> optional = Optional::Find("xz");
	Expect(__LINE__, "x(y)?z finds xz", optional.IsOk() && optional.Value().Group(0) == "xz");
	Expect(__LINE__, "x(y)?z group 1 is unset", optional.IsOk() && !optional.Value().Matched(1));

	using Mixed = Regex<+[] {
		return RegexBuilder("(a)(?:(b)|c)");
	}>;
	Result<Mixed::Captures> mixed = Mixed::Find("ac");
	Expect(__LINE__, "(a)(?:(b)|c) group 1 is a", mixed.IsOk() && mixed.Value().Group<1>() == "a");
	Expect(__LINE__, "(a)(?:(b)|c) group 2 is unset", mixed.IsOk() && !mixed.Value().Matched(2));

	using Empty = Regex<+[] {
		return RegexBuilder("(a?)");
	}>;
	Result<Empty::Captures> empty = Empty::Find("");
	Expect(__LINE__, "(a?) on empty text matched group 1", empty.IsOk() && empty.Value().Matched(1));
	Expect(__LINE__, "(a?) on empty text group 1 is empty", empty.IsOk() && empty.Value().Group<1>().empty());

	using FourEmpty = Regex<+[] {
		return RegexBuilder("(){4}");
	}>;
	Result<FourEmpty::Captures> fourEmpty = FourEmpty::Find("");
	Expect(__LINE__, "(){4} finds the empty text", fourEmpty.IsOk() && fourEmpty.Value().Begin() == 0 && fourEmpty.Value().End() == 0);
	Expect(__LINE__, "(){4} group 1 matched", fourEmpty.IsOk() && fourEmpty.Value().Matched(1));

	using Offsets = Regex<+[] {
		return RegexBuilder("b+");
	}>;
	Result<Offsets::Captures> offsets = Offsets::Find("abbc");
	Expect(__LINE__, "b+ begins at 1", offsets.IsOk() && offsets.Value().Begin() == 1);
	Expect(__LINE__, "b+ ends at 3", offsets.IsOk() && offsets.Value().End() == 3);
	Expect(__LINE__, "b+ text", offsets.IsOk() && offsets.Value().Group(0) == "bb");

	using Ab = Regex<+[] {
		return RegexBuilder("(a)|(b)");
	}>;
	Result<Ab::Captures> second = Ab::Find("xab");
	Expect(__LINE__, "(a)|(b) finds the first a after x", second.IsOk() && second.Value().Begin() == 1 && second.Value().Group(1) == "a");

	using Fields = Regex<+[] {
		return RegexBuilder(R"((\w+)@(\w+))");
	}>;
	Result<Fields::Captures> fields = Fields::Find("me@host");
	Expect(__LINE__, "(\\w+)@(\\w+) group 1", fields.IsOk() && fields.Value().Group(1) == "me");
	Expect(__LINE__, "(\\w+)@(\\w+) group 2", fields.IsOk() && fields.Value().Group(2) == "host");
	Expect(__LINE__, "(\\w+)@(\\w+) group above the count is empty", fields.IsOk() && fields.Value().Group(7).empty());
	Expect(__LINE__, "(\\w+)@(\\w+) group above the count is not matched", fields.IsOk() && !fields.Value().Matched(7));
}


}


#define RX_CASE(pattern, options, text, matches, contains, found, whole) \
	CheckCase<+[] { \
		return RegexBuilder(pattern) options; \
	}>(__LINE__, text, matches, contains, found, whole);


int main() {
	#include "SyntaxCases.inc"
	CaptureChecks();
	std::printf("syntax table: %d cases, %d checks, %d failures\n", caseCount, checks, failures);
	return failures == 0 ? 0 : 1;
}
