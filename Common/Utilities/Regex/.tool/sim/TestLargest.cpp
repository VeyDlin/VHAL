#include <Utilities/Regex/RegexBuilder.h>
#include <cstddef>
#include <cstdio>


// The largest allowed programs, 1024 instructions. The compile time and the memory of this file are the cost check of the parser under
// the default limits of g++ (-fconstexpr-ops-limit). Each program is also checked for its final match and its class count.


// A repeat is at most 255, so the 1023 instructions are four groups of 255 and three more.
using OneClassLargest = RegexCompiled<+[] {
	return RegexBuilder(R"((?:[a-z]{255}){4}[a-z]{3})");
}>;

static_assert(OneClassLargest::Count == 1024);
static_assert(OneClassLargest::ClassCount == 1);
static_assert(OneClassLargest::View.code[1022].operation == RegexOperation::classMatch);
static_assert(OneClassLargest::View.code[1023].operation == RegexOperation::match);


// Thirty-two classes, each a single byte value from 0x00 to 0x1f. The first 31 repeat 32 times and the last 31 times: 1023 instructions
// and the final match make 1024.
using ThirtyTwoClassesLargest = RegexCompiled<+[] {
	return RegexBuilder(R"([\x00]{32}[\x01]{32}[\x02]{32}[\x03]{32}[\x04]{32}[\x05]{32}[\x06]{32}[\x07]{32}[\x08]{32}[\x09]{32}[\x0a]{32}[\x0b]{32}[\x0c]{32}[\x0d]{32}[\x0e]{32}[\x0f]{32}[\x10]{32}[\x11]{32}[\x12]{32}[\x13]{32}[\x14]{32}[\x15]{32}[\x16]{32}[\x17]{32}[\x18]{32}[\x19]{32}[\x1a]{32}[\x1b]{32}[\x1c]{32}[\x1d]{32}[\x1e]{32}[\x1f]{31})");
}>;

static_assert(ThirtyTwoClassesLargest::Count == 1024);
static_assert(ThirtyTwoClassesLargest::ClassCount == 32);
static_assert(ThirtyTwoClassesLargest::View.code[1023].operation == RegexOperation::match);
static_assert(ThirtyTwoClassesLargest::View.code[0].x == 0);
static_assert(ThirtyTwoClassesLargest::View.code[1023 - 31].x == 31);


int main() {
	std::puts("largest programs ok");
	return 0;
}
