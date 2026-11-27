#include <Utilities/Regex/Regex.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>


// The edges of the interface: empty input, the start offset, the input size limits of Find, the repeat maximum and the closure stack
// bound (a long run of empty groups makes the most pending frames).


namespace {


int failures = 0;


void Expect(int line, bool ok) {
	if (!ok) {
		failures++;
		std::printf("FAIL line %d\n", line);
	}
}


// The largest input of Find is 65534 bytes, and the buffer holds one more byte for the 65535-byte case.
std::array<char, 65535> big{};


}


int main() {
	using Empty = Regex<+[] {
		return RegexBuilder("");
	}>;
	Expect(__LINE__, Empty::Matches(""));
	Expect(__LINE__, Empty::Contains(""));
	Result<Empty::Captures> none = Empty::Find("");
	Expect(__LINE__, none.IsOk() && none.Value().Begin() == 0 && none.Value().End() == 0);

	// A start offset equal to the size is allowed, and an empty pattern matches there.
	Result<Empty::Captures> atEnd = Empty::Find("abc", 3);
	Expect(__LINE__, atEnd.IsOk() && atEnd.Value().Begin() == 3);
	Result<Empty::Captures> pastEnd = Empty::Find("abc", 4);
	Expect(__LINE__, !pastEnd.IsOk() && pastEnd.Error() == ResultStatus::invalidParameter);

	using Caret = Regex<+[] {
		return RegexBuilder("^a");
	}>;
	// ^ stays at the start of the whole text, so an offset of 1 does not make the second a a start.
	Result<Caret::Captures> fromOne = Caret::Find("aa", 1);
	Expect(__LINE__, !fromOne.IsOk() && fromOne.Error() == ResultStatus::noMatches);
	Result<Caret::Captures> fromZero = Caret::Find("aa", 0);
	Expect(__LINE__, fromZero.IsOk() && fromZero.Value().Begin() == 0);
	Expect(__LINE__, !Caret::Contains("ba"));

	using Letter = Regex<+[] {
		return RegexBuilder("b");
	}>;
	Result<Letter::Captures> second = Letter::Find("abcb", 2);
	Expect(__LINE__, second.IsOk() && second.Value().Begin() == 3);

	// The largest input that Find takes: 65534 bytes, the last one at offset 65533.
	big.fill('a');
	std::string_view largest(big.data(), 65534);
	using EndA = Regex<+[] {
		return RegexBuilder("a$");
	}>;
	Result<EndA::Captures> atTail = EndA::Find(largest);
	Expect(__LINE__, atTail.IsOk() && atTail.Value().Begin() == 65533 && atTail.Value().End() == 65534);

	// One byte more: Find returns outOfRange, while Matches and Contains take any length.
	std::string_view tooLong(big.data(), 65535);
	Result<EndA::Captures> refused = EndA::Find(tooLong);
	Expect(__LINE__, !refused.IsOk() && refused.Error() == ResultStatus::outOfRange);
	using Star = Regex<+[] {
		return RegexBuilder("a*");
	}>;
	Expect(__LINE__, Star::Matches(tooLong));
	Expect(__LINE__, Star::Contains(tooLong));

	// The repeat maximum is 255, so 255 a's match and 256 do not.
	using Repeat = Regex<+[] {
		return RegexBuilder("a{255}").StackLimit(8192);
	}>;
	Expect(__LINE__, Repeat::Matches(std::string_view(big.data(), 255)));
	Expect(__LINE__, !Repeat::Matches(std::string_view(big.data(), 256)));
	Expect(__LINE__, Repeat::Find(std::string_view(big.data(), 255)).IsOk());

	// Each (){} iteration saves two groups and clears them again. Its clear pushes restores, so the closure stack is sized by CaptureFrames,
	// which is more than the 766 instructions of this pattern. The stack limit is raised for it.
	using EmptyRun = Regex<+[] {
		return RegexBuilder("(){255}").StackLimit(32768);
	}>;
	Result<EmptyRun::Captures> run = EmptyRun::Find("");
	Expect(__LINE__, run.IsOk() && run.Value().Matched(1) && run.Value().Group<1>().empty());

	std::printf("edges: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
