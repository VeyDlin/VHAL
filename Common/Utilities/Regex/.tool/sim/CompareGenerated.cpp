#include "FrameProbe.h"
#include <Utilities/Result/Result.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <regex>
#include <string>
#include <string_view>


// Generated patterns against std::regex (ECMAScript). GeneratedCases.inc is written by GenerateCases.cpp before this file is compiled, and
// it holds one GENERATED line per pattern. For each pattern, eight inputs of 0 to 6 bytes from "ab0 " come from a generator seeded by the
// index of the pattern. The checks are:
//   Matches equals std::regex_match;
//   Contains equals std::regex_search, and equals Find(...).IsOk();
//   Matches implies Contains;
//   Find's group 0 and every group equal the groups of the first std::regex_search match.
// Two known differences skip checks. A quantified body that can match the empty string (mode 1): ECMAScript stops an empty iteration,
// which this code keeps, so the span of the match can differ too, and the span and group checks are skipped. A quantified body that holds
// a capture group (mode 2): libstdc++ keeps a capture from an earlier iteration of a quantified group, where ECMAScript clears it at each
// iteration; for example ((a)|b)+ on "ab" gives group 2 as "a" in std::regex and unset here, which is the ECMAScript result. Only the group
// checks are skipped in mode 2. Patterns that std::regex rejects are counted as skipped and printed.
// The input alphabet has no CR, since '.' differs on CR between the libraries, and the inputs are short, since std::regex recurses.


namespace {


constexpr std::uint32_t InputSeed = 75;
constexpr int InputsPerPattern = 8;
constexpr std::size_t MaxInputLength = 6;

int patterns = 0;
int skippedRegex = 0;
int skippedEmptyBodies = 0;
int skippedRepeatedCaptures = 0;
int inputs = 0;
int mismatches = 0;


std::string MakeInput(std::mt19937& generator) {
	static const char alphabet[] = "ab0 ";
	std::size_t length = generator() % (MaxInputLength + 1);
	std::string text;
	for (std::size_t index = 0; index < length; index++) {
		text += alphabet[generator() % 4];
	}

	return text;
}


void Mismatch(int index, const char* pattern, const std::string& text, const char* what) {
	mismatches++;
	std::printf("MISMATCH pattern %d (%s) input \"%s\": %s\n", index, pattern, text.c_str(), what);
}


template<RegexBuilder (*Make)()>
void RunPattern(int index, const char* pattern, int mode) {
	using Pattern = Regex<Make>;
	std::regex expression;
	try {
		expression = std::regex(pattern, std::regex::ECMAScript);
	} catch (const std::regex_error&) {
		skippedRegex++;
		std::printf("SKIP pattern %d (%s): std::regex rejects it\n", index, pattern);
		return;
	}

	patterns++;
	if (mode == 1) {
		skippedEmptyBodies++;
	} else if (mode == 2) {
		skippedRepeatedCaptures++;
	}

	std::mt19937 generator(InputSeed + static_cast<std::uint32_t>(index));
	for (int round = 0; round < InputsPerPattern; round++) {
		std::string text = MakeInput(generator);
		inputs++;
		std::string_view view(text);
		bool expectMatches = std::regex_match(text, expression);
		bool expectContains = std::regex_search(text, expression);
		std::smatch first;
		bool expectFound = std::regex_search(text, first, expression);
		bool gotMatches = Pattern::Matches(view);
		bool gotContains = Pattern::Contains(view);
		RegexProbe::Reset();
		Result<typename Pattern::Captures> found = Pattern::Find(view);
		if (RegexProbe::highWater > Pattern::CaptureFrames) {
			Mismatch(index, pattern, text, "the closure stack is above CaptureFrames");
		}

		if (gotMatches != expectMatches) {
			Mismatch(index, pattern, text, "Matches differs from std::regex_match");
		}

		if (gotContains != expectContains) {
			Mismatch(index, pattern, text, "Contains differs from std::regex_search");
		}

		if (gotContains != found.IsOk()) {
			Mismatch(index, pattern, text, "Contains differs from Find");
		}

		if (gotMatches && !gotContains) {
			Mismatch(index, pattern, text, "Matches without Contains");
		}

		if (expectFound != expectContains) {
			Mismatch(index, pattern, text, "the reference search is inconsistent");
		}

		if (!found.IsOk() || mode == 1) {
			continue;
		}

		if (std::string(found.Value().Group(0)) != first.str(0)) {
			Mismatch(index, pattern, text, "group 0 differs from std::regex_search");
		}

		if (mode == 2) {
			continue;
		}

		for (std::size_t group = 1; group <= Pattern::GroupCount; group++) {
			bool matched = first[group].matched;
			if (found.Value().Matched(group) != matched) {
				Mismatch(index, pattern, text, "a group's matched flag differs from std::regex_search");
			} else if (matched && std::string(found.Value().Group(group)) != first.str(group)) {
				Mismatch(index, pattern, text, "a group's text differs from std::regex_search");
			}
		}
	}
}


}


#define GENERATED(index, pattern, skip) \
	RunPattern<+[] { \
		return RegexBuilder(pattern).StackLimit(65536); \
	}>(index, pattern, skip);


// run.sh sets REGEX_CASES_FILE to the part of the list that this translation unit holds.
#ifndef REGEX_CASES_FILE
#define REGEX_CASES_FILE "GeneratedCases.inc"
#endif


int main() {
	#include REGEX_CASES_FILE
	std::printf("generated patterns: %d compared, %d skipped by std::regex, %d with the span and group checks skipped (empty iteration), %d with the group checks skipped (repeated capture)\n", patterns, skippedRegex, skippedEmptyBodies, skippedRepeatedCaptures);
	std::printf("generated inputs: %d, mismatches: %d\n", inputs, mismatches);
	return mismatches == 0 ? 0 : 1;
}
