#include <cstddef>
#include <cstdio>
#include <regex>
#include <string>
#include <string_view>


// The syntax table of SyntaxCases.inc, run through std::regex (ECMAScript). The expected results of the table were written by hand, so
// this file checks the table against an independent engine: std::regex_match for Matches, std::regex_search for Contains, and the first
// match of std::regex_search for Find. A pattern that std::regex rejects is counted as skipped and printed.
// This file uses exceptions and RTTI, which std::regex needs, so run.sh builds it without the device flags of -fno-exceptions and -fno-rtti.


namespace {


int compared = 0;
int skipped = 0;
int mismatches = 0;


void CompareCase(int line, const char* pattern, std::string_view options, std::string_view text, bool matches, bool contains, bool found, std::string_view whole) {
	std::regex::flag_type flags = std::regex::ECMAScript;
	if (options.find("IgnoreCase") != std::string_view::npos) {
		flags |= std::regex::icase;
	}

	std::regex expression;
	try {
		expression = std::regex(pattern, flags);
	} catch (const std::regex_error& error) {
		skipped++;
		std::printf("SKIP line %d: std::regex rejects the pattern (%s)\n", line, error.what());
		return;
	}

	compared++;
	std::string subject(text);
	bool gotMatches = std::regex_match(subject, expression);
	bool gotContains = std::regex_search(subject, expression);
	std::smatch first;
	bool gotFound = std::regex_search(subject, first, expression);
	bool ok = gotMatches == matches && gotContains == contains && gotFound == found;
	if (ok && found) {
		ok = first[0].str() == std::string(whole);
	}

	if (!ok) {
		mismatches++;
		std::printf("MISMATCH line %d: std::regex gives matches=%d contains=%d found=%d\n", line, gotMatches, gotContains, gotFound);
	}
}


}


#define RX_CASE(pattern, options, text, matches, contains, found, whole) \
	CompareCase(__LINE__, pattern, #options, text, matches, contains, found, whole);


int main() {
	#include "SyntaxCases.inc"
	std::printf("std::regex syntax table: %d compared, %d skipped, %d mismatches\n", compared, skipped, mismatches);
	return mismatches == 0 ? 0 : 1;
}
