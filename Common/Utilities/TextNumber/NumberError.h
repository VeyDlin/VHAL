#pragma once
#include <cstddef>
#include <cstdint>


// Why a number text was refused. Every parser and whole-string helper reports one of these.
enum class NumberErrorCode : uint8_t {
	None,
	Empty,
	InvalidCharacter,
	SignNotAllowed,
	OutOfRange,
	PrefixWithoutDigits,
	ExponentWithoutDigits
};


// The error with the data a message needs. position is the index of the character that caused it, or the number of
// characters accepted when the input ended. minimum and maximum hold the allowed range for OutOfRange; floats leave them zero.
template <typename T>
struct NumberError {
	NumberErrorCode code = NumberErrorCode::None;
	size_t position = 0;
	T minimum = T{};
	T maximum = T{};


	constexpr bool IsError() const {
		return code != NumberErrorCode::None;
	}
};


// The value or the error, and how far the text was read. endPosition counts the characters that were accepted.
template <typename T>
struct NumberResult {
	T value = T{};
	NumberError<T> error{};
	size_t endPosition = 0;


	constexpr bool IsOk() const {
		return !error.IsError();
	}
};
