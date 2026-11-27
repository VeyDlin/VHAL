#pragma once
#include <array>
#include <cstddef>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/CompileTime/Require.h>


// One code point decoded from UTF-8. length is 0 when the bytes at the position are not a valid sequence.
struct USBUtf8Character {
	std::size_t length = 0;
	uint32 codePoint = 0;
};


// Decodes the sequence that starts at index. Overlong forms, surrogates (U+D800 to U+DFFF, which UTF-16 uses only in pairs)
// and code points above U+10FFFF are rejected: UTF-16LE cannot carry them.
constexpr USBUtf8Character USBDecodeUtf8(const char* text, std::size_t size, std::size_t index) {
	USBUtf8Character character;
	if (index >= size) {
		return character;
	}

	uint8 lead = static_cast<uint8>(text[index]);
	std::size_t length = 0;
	uint32 codePoint = 0;
	uint32 minimum = 0;
	if (lead < 0x80) {
		length = 1;
		codePoint = lead;
	} else if ((lead & 0xE0) == 0xC0) {
		length = 2;
		codePoint = lead & 0x1F;
		minimum = 0x80;
	} else if ((lead & 0xF0) == 0xE0) {
		length = 3;
		codePoint = lead & 0x0F;
		minimum = 0x800;
	} else if ((lead & 0xF8) == 0xF0) {
		length = 4;
		codePoint = lead & 0x07;
		minimum = 0x10000;
	} else {
		return character;
	}

	if (index + length > size) {
		return character;
	}

	for (std::size_t offset = 1; offset < length; offset++) {
		uint8 continuation = static_cast<uint8>(text[index + offset]);
		if ((continuation & 0xC0) != 0x80) {
			return character;
		}

		codePoint = (codePoint << 6) | (continuation & 0x3F);
	}

	if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
		return character;
	}

	character.length = length;
	character.codePoint = codePoint;
	return character;
}


struct USBUtf16Count {
	bool valid = true;
	std::size_t units = 0;
};


constexpr USBUtf16Count USBCountUtf16Units(const char* text, std::size_t size) {
	USBUtf16Count count;
	std::size_t index = 0;
	while (index < size) {
		USBUtf8Character character = USBDecodeUtf8(text, size, index);
		if (character.length == 0) {
			count.valid = false;
			return count;
		}

		count.units += character.codePoint >= 0x10000 ? 2 : 1;
		index += character.length;
	}

	return count;
}


// Writes the text as UTF-16LE at offset and returns the offset after it. bytes must hold the encoded units.
template<std::size_t Size>
constexpr std::size_t USBEncodeUtf16(
	const char* text,
	std::size_t textSize,
	std::array<uint8, Size>& bytes,
	std::size_t offset)
{
	std::size_t index = 0;
	while (index < textSize) {
		USBUtf8Character character = USBDecodeUtf8(text, textSize, index);
		Require<"USB: string is not valid UTF-8">(character.length != 0);
		if (character.codePoint < 0x10000) {
			USBDescriptorPut16(bytes, offset, static_cast<uint16>(character.codePoint));
			offset += 2;
		} else {
			uint32 shifted = character.codePoint - 0x10000;
			USBDescriptorPut16(bytes, offset, static_cast<uint16>(0xD800 + (shifted >> 10)));
			USBDescriptorPut16(bytes, offset + 2, static_cast<uint16>(0xDC00 + (shifted & 0x3FF)));
			offset += 4;
		}

		index += character.length;
	}

	return offset;
}


// Number of UTF-16 code units the text encodes to, without a terminator. Invalid text fails the static_assert.
template<TextLiteral Text>
constexpr std::size_t USBUtf16UnitCount() {
	constexpr USBUtf16Count count = USBCountUtf16Units(Text.value, Text.GetLength());
	static_assert(count.valid, "text must be valid UTF-8 with no surrogate and no code point above U+10FFFF");
	return count.units;
}


// The text as UTF-16LE bytes, without a terminator.
template<TextLiteral Text>
constexpr std::array<uint8, 2 * USBUtf16UnitCount<Text>()> MakeUSBUtf16LE() {
	std::array<uint8, 2 * USBUtf16UnitCount<Text>()> bytes{};
	USBEncodeUtf16(Text.value, Text.GetLength(), bytes, 0);
	return bytes;
}


// The text as UTF-16LE bytes followed by a 16-bit NUL. Registry names and REG_SZ values are stored this way.
template<TextLiteral Text>
constexpr std::array<uint8, 2 * (USBUtf16UnitCount<Text>() + 1)> MakeUSBUtf16LENullTerminated() {
	std::array<uint8, 2 * (USBUtf16UnitCount<Text>() + 1)> bytes{};
	USBEncodeUtf16(Text.value, Text.GetLength(), bytes, 0);
	return bytes;
}


// String descriptor (USB 2.0 section 9.6.7): bLength, bDescriptorType 3, then the UTF-16LE text. bLength is one byte, so
// the text is limited to 126 code units.
template<TextLiteral Text>
constexpr std::array<uint8, 2 + 2 * USBUtf16UnitCount<Text>()> MakeUSBStringDescriptor() {
	constexpr std::size_t units = USBUtf16UnitCount<Text>();
	static_assert(units <= 126, "a string descriptor holds at most 126 UTF-16 code units");
	std::array<uint8, 2 + 2 * units> bytes{};
	bytes[0] = static_cast<uint8>(2 + 2 * units);
	bytes[1] = static_cast<uint8>(USBDescriptorType::String);
	USBEncodeUtf16(Text.value, Text.GetLength(), bytes, 2);
	return bytes;
}


// String descriptor 0: the supported language IDs. The stack answers with the one language given here, so this takes one.
constexpr std::array<uint8, 4> MakeUSBLanguageIdDescriptor(uint16 languageId) {
	Require<"USB: language ID 0 names no language">(languageId != 0);
	std::array<uint8, 4> bytes{};
	bytes[0] = 4;
	bytes[1] = static_cast<uint8>(USBDescriptorType::String);
	USBDescriptorPut16(bytes, 2, languageId);
	return bytes;
}
