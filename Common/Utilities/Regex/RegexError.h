#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/CompileTime/TextLiteral.h>
#include <cstddef>


// The errors of the pattern parser and of the matching checks. The parser stores the first failure as a code and a value. The value is
// an offset in the pattern, or a count, or a number of bytes. RegexRaise turns the code into a call to RegexError<Message, Value>, which
// is declared and never defined, so the compiler prints the message and the value in the error.
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


// Declared, never defined. A call from a constant expression is ill-formed, and the compiler prints the message and the value.
template<TextLiteral Message, std::size_t Value>
void RegexError();


// Calls RegexError with the message of a code. A code of none does nothing.
template<RegexErrorCode Code, std::size_t Value>
constexpr void RegexRaise() {
	if constexpr (Code == RegexErrorCode::unbalancedClose) {
		RegexError<TextLiteral{"unbalanced ')'"}, Value>();
	} else if constexpr (Code == RegexErrorCode::unclosedGroup) {
		RegexError<TextLiteral{"missing ')'"}, Value>();
	} else if constexpr (Code == RegexErrorCode::unclosedClass) {
		RegexError<TextLiteral{"missing ']'"}, Value>();
	} else if constexpr (Code == RegexErrorCode::emptyClass) {
		RegexError<TextLiteral{"empty class"}, Value>();
	} else if constexpr (Code == RegexErrorCode::reversedRange) {
		RegexError<TextLiteral{"reversed range"}, Value>();
	} else if constexpr (Code == RegexErrorCode::classEscapeRange) {
		RegexError<TextLiteral{"a class escape cannot start or end a range"}, Value>();
	} else if constexpr (Code == RegexErrorCode::nothingToRepeat) {
		RegexError<TextLiteral{"nothing to repeat"}, Value>();
	} else if constexpr (Code == RegexErrorCode::repeatAbove255) {
		RegexError<TextLiteral{"repeat count above 255"}, Value>();
	} else if constexpr (Code == RegexErrorCode::repeatMinimumAboveMaximum) {
		RegexError<TextLiteral{"repeat minimum above maximum"}, Value>();
	} else if constexpr (Code == RegexErrorCode::bareBrace) {
		RegexError<TextLiteral{"escape '{' and '}' that are not quantifiers"}, Value>();
	} else if constexpr (Code == RegexErrorCode::bareBracket) {
		RegexError<TextLiteral{"escape ']' outside a class"}, Value>();
	} else if constexpr (Code == RegexErrorCode::unknownEscape) {
		RegexError<TextLiteral{"unknown escape"}, Value>();
	} else if constexpr (Code == RegexErrorCode::escapeNul) {
		RegexError<TextLiteral{"use \\x00 for NUL"}, Value>();
	} else if constexpr (Code == RegexErrorCode::hexEscape) {
		RegexError<TextLiteral{"\\x takes two hex digits"}, Value>();
	} else if constexpr (Code == RegexErrorCode::trailingBackslash) {
		RegexError<TextLiteral{"pattern ends with a backslash"}, Value>();
	} else if constexpr (Code == RegexErrorCode::backreference) {
		RegexError<TextLiteral{"backreferences are not supported"}, Value>();
	} else if constexpr (Code == RegexErrorCode::lookaround) {
		RegexError<TextLiteral{"lookaround is not supported"}, Value>();
	} else if constexpr (Code == RegexErrorCode::namedGroup) {
		RegexError<TextLiteral{"named groups are not supported"}, Value>();
	} else if constexpr (Code == RegexErrorCode::inlineFlags) {
		RegexError<TextLiteral{"inline flags are not supported, use IgnoreCase()"}, Value>();
	} else if constexpr (Code == RegexErrorCode::unknownGroup) {
		RegexError<TextLiteral{"unknown group syntax"}, Value>();
	} else if constexpr (Code == RegexErrorCode::wordBoundary) {
		RegexError<TextLiteral{"word boundaries are not supported"}, Value>();
	} else if constexpr (Code == RegexErrorCode::tooManyGroups) {
		RegexError<TextLiteral{"more than 15 groups"}, Value>();
	} else if constexpr (Code == RegexErrorCode::tooManyParts) {
		RegexError<TextLiteral{"pattern has too many parts"}, Value>();
	} else if constexpr (Code == RegexErrorCode::instructionLimit) {
		RegexError<TextLiteral{"program is over the instruction limit"}, Value>();
	} else if constexpr (Code == RegexErrorCode::classLimit) {
		RegexError<TextLiteral{"program is over the class limit"}, Value>();
	} else if constexpr (Code == RegexErrorCode::plainStack) {
		RegexError<TextLiteral{"matching needs more stack than StackLimit allows"}, Value>();
	} else if constexpr (Code == RegexErrorCode::capturesStack) {
		RegexError<TextLiteral{"captures need more stack than StackLimit allows"}, Value>();
	} else if constexpr (Code == RegexErrorCode::writeMismatch) {
		RegexError<TextLiteral{"the program fill does not match its count"}, Value>();
	}
}


// The check of the plain matching loop against StackLimit. Needed is PlainStackBytes and Limit is StackLimit.
template<std::size_t Needed, std::size_t Limit>
constexpr void RegexRequirePlainStack() {
	if constexpr (Needed > Limit) {
		RegexError<TextLiteral{"matching needs more stack than StackLimit allows"}, Needed>();
	}
}


// The check of the captures loop against StackLimit. Needed is CapturesStackBytes and Limit is StackLimit.
template<std::size_t Needed, std::size_t Limit>
constexpr void RegexRequireCapturesStack() {
	if constexpr (Needed > Limit) {
		RegexError<TextLiteral{"captures need more stack than StackLimit allows"}, Needed>();
	}
}


// The check of a group index taken as a template argument.
template<std::size_t Index, std::size_t Groups>
constexpr void RegexRequireGroup() {
	static_assert(Index <= Groups, "group index is above the group count");
}
