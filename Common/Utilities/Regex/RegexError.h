#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/CompileTime/Require.h>
#include <cstddef>


// The errors of the pattern parser and of the matching checks. The parser stores the first failure as a code and a value. The value is
// an offset in the pattern, or a count, or a number of bytes. RegexRaise turns the code into a RequireValue check, which fails with the
// message and the value in the error.
enum class RegexErrorCode : uint8 {
	none,
	unbalancedClose,
	unclosedGroup,
	unclosedClass,
	emptyClass,
	reversedRange,
	classEscapeRange,
	nothingToRepeat,
	repeatAbove255,
	repeatMinimumAboveMaximum,
	bareBrace,
	bareBracket,
	unknownEscape,
	escapeNul,
	hexEscape,
	trailingBackslash,
	backreference,
	lookaround,
	namedGroup,
	inlineFlags,
	unknownGroup,
	wordBoundary,
	tooManyGroups,
	tooManyParts,
	instructionLimit,
	classLimit,
	plainStack,
	capturesStack,
	writeMismatch
};


struct RegexFailure {
	RegexErrorCode code = RegexErrorCode::none;
	std::size_t value = 0;
};


// Fails the check with the message of a code. A code of none does nothing.
template<RegexErrorCode Code, std::size_t Value>
constexpr void RegexRaise() {
	if constexpr (Code == RegexErrorCode::unbalancedClose) {
		RequireValue<"Regex:unbalanced ')'", Value>(false);
	} else if constexpr (Code == RegexErrorCode::unclosedGroup) {
		RequireValue<"Regex:missing ')'", Value>(false);
	} else if constexpr (Code == RegexErrorCode::unclosedClass) {
		RequireValue<"Regex:missing ']'", Value>(false);
	} else if constexpr (Code == RegexErrorCode::emptyClass) {
		RequireValue<"Regex:empty class", Value>(false);
	} else if constexpr (Code == RegexErrorCode::reversedRange) {
		RequireValue<"Regex:reversed range", Value>(false);
	} else if constexpr (Code == RegexErrorCode::classEscapeRange) {
		RequireValue<"Regex:a class escape cannot start or end a range", Value>(false);
	} else if constexpr (Code == RegexErrorCode::nothingToRepeat) {
		RequireValue<"Regex:nothing to repeat", Value>(false);
	} else if constexpr (Code == RegexErrorCode::repeatAbove255) {
		RequireValue<"Regex:repeat count above 255", Value>(false);
	} else if constexpr (Code == RegexErrorCode::repeatMinimumAboveMaximum) {
		RequireValue<"Regex:repeat minimum above maximum", Value>(false);
	} else if constexpr (Code == RegexErrorCode::bareBrace) {
		RequireValue<"Regex:escape '{' and '}' that are not quantifiers", Value>(false);
	} else if constexpr (Code == RegexErrorCode::bareBracket) {
		RequireValue<"Regex:escape ']' outside a class", Value>(false);
	} else if constexpr (Code == RegexErrorCode::unknownEscape) {
		RequireValue<"Regex:unknown escape", Value>(false);
	} else if constexpr (Code == RegexErrorCode::escapeNul) {
		RequireValue<"Regex:use \\x00 for NUL", Value>(false);
	} else if constexpr (Code == RegexErrorCode::hexEscape) {
		RequireValue<"Regex:\\x takes two hex digits", Value>(false);
	} else if constexpr (Code == RegexErrorCode::trailingBackslash) {
		RequireValue<"Regex:pattern ends with a backslash", Value>(false);
	} else if constexpr (Code == RegexErrorCode::backreference) {
		RequireValue<"Regex:backreferences are not supported", Value>(false);
	} else if constexpr (Code == RegexErrorCode::lookaround) {
		RequireValue<"Regex:lookaround is not supported", Value>(false);
	} else if constexpr (Code == RegexErrorCode::namedGroup) {
		RequireValue<"Regex:named groups are not supported", Value>(false);
	} else if constexpr (Code == RegexErrorCode::inlineFlags) {
		RequireValue<"Regex:inline flags are not supported, use IgnoreCase()", Value>(false);
	} else if constexpr (Code == RegexErrorCode::unknownGroup) {
		RequireValue<"Regex:unknown group syntax", Value>(false);
	} else if constexpr (Code == RegexErrorCode::wordBoundary) {
		RequireValue<"Regex:word boundaries are not supported", Value>(false);
	} else if constexpr (Code == RegexErrorCode::tooManyGroups) {
		RequireValue<"Regex:more than 15 groups", Value>(false);
	} else if constexpr (Code == RegexErrorCode::tooManyParts) {
		RequireValue<"Regex:pattern has too many parts", Value>(false);
	} else if constexpr (Code == RegexErrorCode::instructionLimit) {
		RequireValue<"Regex:program is over the instruction limit", Value>(false);
	} else if constexpr (Code == RegexErrorCode::classLimit) {
		RequireValue<"Regex:program is over the class limit", Value>(false);
	} else if constexpr (Code == RegexErrorCode::plainStack) {
		RequireValue<"Regex:matching needs more stack than StackLimit allows", Value>(false);
	} else if constexpr (Code == RegexErrorCode::capturesStack) {
		RequireValue<"Regex:captures need more stack than StackLimit allows", Value>(false);
	} else if constexpr (Code == RegexErrorCode::writeMismatch) {
		RequireValue<"Regex:the program fill does not match its count", Value>(false);
	}
}


// The check of the plain matching loop against StackLimit. Needed is PlainStackBytes and Limit is StackLimit.
template<std::size_t Needed, std::size_t Limit>
constexpr void RegexRequirePlainStack() {
	if constexpr (Needed > Limit) {
		RequireValue<"Regex:matching needs more stack than StackLimit allows", Needed>(false);
	}
}


// The check of the captures loop against StackLimit. Needed is CapturesStackBytes and Limit is StackLimit.
template<std::size_t Needed, std::size_t Limit>
constexpr void RegexRequireCapturesStack() {
	if constexpr (Needed > Limit) {
		RequireValue<"Regex:captures need more stack than StackLimit allows", Needed>(false);
	}
}


// The check of a group index taken as a template argument.
template<std::size_t Index, std::size_t Groups>
constexpr void RegexRequireGroup() {
	static_assert(Index <= Groups, "group index is above the group count");
}
