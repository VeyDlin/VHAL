#pragma once
#include "RegexError.h"
#include "RegexProgram.h"
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <string_view>


// A pattern is a captureless lambda that returns a RegexBuilder, as the FAT12 image and the USB descriptors take their chains.
// RegexCompiled evaluates the chain at compile time and gives the program in flash:
//
//   using Url = RegexCompiled<+[] {
//       return RegexBuilder(R"(^(https?)://([a-z0-9.-]+)(?::([0-9]{1,5}))?(/[^ ]*)?$)")
//           .IgnoreCase()
//           .StackLimit(3072);
//   }>;
//
// The parser runs in two passes. The first pass parses the pattern into a tree of nodes and checks every rule. It also counts the
// instructions and the classes. The second pass writes the program into arrays of exactly the counted size, and a size that does not
// match is a compile error. Every failure is a RegexError with its message and its value (see RegexError.h).
//
// Syntax, as the regex design lists it: literals, ., classes, the shorthand escapes, ^ and $, capturing and (?:) groups, alternation,
// the quantifiers * + ? {m} {m,} {m,n} with one optional lazy ?, and IgnoreCase() for ASCII letters. Anything else is an error.


// The nodes of the parser pool. A node is a pattern item, and the program of a node is written in the second pass.
constexpr std::size_t RegexMaxNodes = 2 * RegexMaxInstructions;
// The count of a program is kept below this value, so the saturating sums of the first pass never overflow.
constexpr std::size_t RegexCountCap = 1048576;


// The pattern builder. The pattern is a string literal, and the options are set by the chain.
class RegexBuilder {
private:
	std::string_view pattern;
	bool ignoreCase = false;
	std::size_t stackLimit = RegexDefaultStackLimit;


public:
	constexpr explicit RegexBuilder(std::string_view text) : pattern(text) {
	}


	// ASCII letters match in either case, in literals and in classes.
	constexpr RegexBuilder& IgnoreCase() {
		ignoreCase = true;
		return *this;
	}


	// The stack bytes the matching loops may take. A pattern whose workspace is larger does not compile.
	constexpr RegexBuilder& StackLimit(std::size_t bytes) {
		stackLimit = bytes;
		return *this;
	}


	constexpr std::string_view Pattern() const {
		return pattern;
	}


	constexpr bool IsIgnoreCase() const {
		return ignoreCase;
	}


	constexpr std::size_t StackLimitBytes() const {
		return stackLimit;
	}
};


// The kinds of parser nodes. Items of a concat or an alternate are linked through next.
enum class RegexNodeKind : uint8 {
	empty,
	character,
	characterFold,
	any,
	classMatch,
	begin,
	end,
	group,
	concat,
	alternate,
	repeat
};


// A parser node. The fields that a kind does not use stay zero.
struct RegexNode {
	RegexNodeKind kind = RegexNodeKind::empty;
	uint8 value = 0;
	uint8 min = 0;
	uint8 groupFirst = 0;
	uint8 groupEnd = 0;
	bool lazy = false;
	uint16 max = 0;
	uint16 child = 0;
	uint16 next = 0;
};


// The result of the first pass: the failure, or the instruction count, the class count and the group count.
struct RegexSummary {
	RegexFailure failure{};
	std::size_t instructionCount = 0;
	std::size_t classCount = 0;
	std::size_t groupCount = 0;
};


constexpr bool RegexIsDigit(char value) {
	return value >= '0' && value <= '9';
}


constexpr bool RegexIsHex(char value) {
	return RegexIsDigit(value) || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}


constexpr uint8 RegexHexValue(char value) {
	if (RegexIsDigit(value)) {
		return static_cast<uint8>(value - '0');
	}

	if (value >= 'a' && value <= 'f') {
		return static_cast<uint8>(value - 'a' + 10);
	}

	return static_cast<uint8>(value - 'A' + 10);
}


constexpr bool RegexIsLetter(char value) {
	return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}


constexpr uint8 RegexLower(char value) {
	return static_cast<uint8>((value >= 'A' && value <= 'Z') ? value - 'A' + 'a' : value);
}


// The ASCII punctuation. A backslash before one of these marks is that mark.
constexpr bool RegexIsPunctuation(char value) {
	for (char mark : std::string_view("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~")) {
		if (value == mark) {
			return true;
		}
	}

	return false;
}


// The ECMAScript flag letters that can follow (? in a group. Any of them is an inline flag group, which is an error.
constexpr bool RegexIsFlag(char value) {
	for (char flag : std::string_view("dgimsuvy")) {
		if (value == flag) {
			return true;
		}
	}

	return false;
}


// The kinds that take one instruction each.
constexpr bool RegexIsLeaf(RegexNodeKind kind) {
	return kind == RegexNodeKind::character || kind == RegexNodeKind::characterFold || kind == RegexNodeKind::any
		|| kind == RegexNodeKind::classMatch || kind == RegexNodeKind::begin || kind == RegexNodeKind::end;
}


// Adds two counts, and keeps the sum at the cap.
constexpr std::size_t RegexCapAdd(std::size_t left, std::size_t right) {
	std::size_t sum = left + right;
	return sum > RegexCountCap ? RegexCountCap : sum;
}


// Multiplies two counts, and keeps the product at the cap.
constexpr std::size_t RegexCapMul(std::size_t left, std::size_t right) {
	if (left != 0 && right > RegexCountCap / left) {
		return RegexCountCap;
	}

	std::size_t product = left * right;
	return product > RegexCountCap ? RegexCountCap : product;
}


// The parser of a pattern. The first pass builds the nodes and checks the rules. The second pass writes the instructions.
class RegexParser {
private:
	std::string_view pattern;
	bool ignoreCase = false;
	std::size_t position = 0;
	std::size_t groupCount = 0;
	std::size_t nodeCount = 1;
	std::size_t classCount = 0;
	std::size_t emitted = 0;
	std::size_t capacity = 0;
	uint16 root = 0;
	RegexFailure failure{};
	RegexInstruction* output = nullptr;
	std::array<RegexNode, RegexMaxNodes> nodes{};
	std::array<RegexClass, RegexMaxClasses> classList{};


public:
	constexpr explicit RegexParser(const RegexBuilder& builder) : pattern(builder.Pattern()), ignoreCase(builder.IsIgnoreCase()) {
	}


	// The first pass: parses the whole pattern and stops at the first failure.
	constexpr void Parse() {
		root = ParseAlternation();
		if (!Failed() && position < pattern.size()) {
			Fail(RegexErrorCode::unbalancedClose, position);
		}
	}


	constexpr bool Failed() const {
		return failure.code != RegexErrorCode::none;
	}


	constexpr RegexFailure GetFailure() const {
		return failure;
	}


	constexpr std::size_t GetGroupCount() const {
		return groupCount;
	}


	constexpr std::size_t GetClassCount() const {
		return classCount;
	}


	// The instruction count of the program, including the final match. Valid after a successful Parse().
	constexpr std::size_t Count() const {
		return RegexCapAdd(SizeOf(root), 1);
	}


	// The second pass: writes the program and the classes into arrays of the counted sizes. A count that does not match is a failure.
	constexpr void Write(RegexInstruction* code, std::size_t instructionCount, RegexClass* classArray, std::size_t classExpected) {
		output = code;
		capacity = instructionCount;
		EmitNode(root);
		Emit(RegexOperation::match, 0, 0, 0);
		if (emitted != instructionCount) {
			Fail(RegexErrorCode::writeMismatch, emitted);
			return;
		}

		if (classCount != classExpected) {
			Fail(RegexErrorCode::writeMismatch, classCount);
			return;
		}

		for (std::size_t index = 0; index < classCount; index++) {
			classArray[index] = classList[index];
		}
	}


private:
	constexpr void Fail(RegexErrorCode code, std::size_t value) {
		if (!Failed()) {
			failure.code = code;
			failure.value = value;
		}
	}


	constexpr bool AtEnd() const {
		return position >= pattern.size();
	}


	// True when the byte at index is the given one.
	constexpr bool Is(std::size_t index, char value) const {
		return index < pattern.size() && pattern[index] == value;
	}


	constexpr uint16 AddNode(RegexNodeKind kind, uint8 value) {
		if (nodeCount >= RegexMaxNodes) {
			Fail(RegexErrorCode::tooManyParts, RegexMaxNodes);
			return 0;
		}

		std::size_t index = nodeCount;
		nodeCount++;
		nodes[index].kind = kind;
		nodes[index].value = value;
		return static_cast<uint16>(index);
	}


	constexpr uint16 AddCharacter(uint8 value) {
		if (ignoreCase && RegexIsLetter(static_cast<char>(value))) {
			return AddNode(RegexNodeKind::characterFold, RegexLower(static_cast<char>(value)));
		}

		return AddNode(RegexNodeKind::character, value);
	}


	// Finds the bitmap in the class table, or adds it. The table holds the classes in the order of their first use.
	constexpr uint8 FindOrAddClass(const RegexClass& bits) {
		for (std::size_t index = 0; index < classCount; index++) {
			if (classList[index] == bits) {
				return static_cast<uint8>(index);
			}
		}

		if (classCount >= RegexMaxClasses) {
			Fail(RegexErrorCode::classLimit, RegexMaxClasses + 1);
			return 0;
		}

		classList[classCount] = bits;
		classCount++;
		return static_cast<uint8>(classCount - 1);
	}


	constexpr uint16 AddClassNode(const RegexClass& bits) {
		uint8 index = FindOrAddClass(bits);
		if (Failed()) {
			return 0;
		}

		return AddNode(RegexNodeKind::classMatch, index);
	}


	// Reads ) at the end of a group, or fails at the ( that opened it.
	constexpr void ExpectClose(std::size_t open) {
		if (!Failed() && Is(position, ')')) {
			position++;
			return;
		}

		Fail(RegexErrorCode::unclosedGroup, open);
	}


	// alternate: sequence ('|' sequence)*
	constexpr uint16 ParseAlternation() {
		uint16 first = ParseSequence();
		if (Failed() || !Is(position, '|')) {
			return first;
		}

		uint16 alternate = AddNode(RegexNodeKind::alternate, 0);
		if (Failed()) {
			return 0;
		}

		nodes[alternate].child = first;
		uint16 tail = first;
		while (!Failed() && Is(position, '|')) {
			position++;
			uint16 item = ParseSequence();
			if (Failed()) {
				return 0;
			}

			nodes[tail].next = item;
			tail = item;
		}

		return alternate;
	}


	// sequence: term*, up to the end, a | or a )
	constexpr uint16 ParseSequence() {
		uint16 first = 0;
		uint16 tail = 0;
		std::size_t count = 0;
		while (!Failed() && !AtEnd() && !Is(position, '|') && !Is(position, ')')) {
			uint16 item = ParseTerm();
			if (Failed()) {
				return 0;
			}

			if (count == 0) {
				first = item;
			} else {
				nodes[tail].next = item;
			}

			tail = item;
			count++;
		}

		if (Failed()) {
			return 0;
		}

		if (count == 0) {
			return AddNode(RegexNodeKind::empty, 0);
		}

		if (count == 1) {
			return first;
		}

		uint16 concat = AddNode(RegexNodeKind::concat, 0);
		if (Failed()) {
			return 0;
		}

		nodes[concat].child = first;
		return concat;
	}


	// term: an anchor, or an atom with its optional quantifier
	constexpr uint16 ParseTerm() {
		std::size_t groupsBefore = groupCount;
		if (Is(position, '^') || Is(position, '$')) {
			RegexNodeKind kind = Is(position, '^') ? RegexNodeKind::begin : RegexNodeKind::end;
			uint16 anchor = AddNode(kind, 0);
			position++;
			if (IsQuantifierAt(position)) {
				Fail(RegexErrorCode::nothingToRepeat, position);
			}

			return anchor;
		}

		uint16 atom = ParseAtom();
		if (Failed()) {
			return 0;
		}

		return ParseQuantifier(atom, groupsBefore);
	}


	constexpr uint16 ParseAtom() {
		char value = pattern[position];
		if (value == '(') {
			return ParseGroup();
		}

		if (value == '[') {
			return ParseClass();
		}

		if (value == '\\') {
			return ParseEscapeAtom();
		}

		if (value == '.') {
			position++;
			return AddNode(RegexNodeKind::any, 0);
		}

		if (value == '*' || value == '+' || value == '?') {
			Fail(RegexErrorCode::nothingToRepeat, position);
			return 0;
		}

		if (value == '{') {
			if (IsBraceAt(position)) {
				Fail(RegexErrorCode::nothingToRepeat, position);
			} else {
				Fail(RegexErrorCode::bareBrace, position);
			}

			return 0;
		}

		if (value == '}') {
			Fail(RegexErrorCode::bareBrace, position);
			return 0;
		}

		if (value == ']') {
			Fail(RegexErrorCode::bareBracket, position);
			return 0;
		}

		position++;
		return AddCharacter(static_cast<uint8>(value));
	}


	// group: (...), (?:...), or an error for the other (? forms
	constexpr uint16 ParseGroup() {
		std::size_t open = position;
		position++;
		if (Is(position, '?')) {
			return ParseSpecialGroup(open);
		}

		if (groupCount >= RegexMaxGroups) {
			Fail(RegexErrorCode::tooManyGroups, open);
			return 0;
		}

		groupCount++;
		uint8 index = static_cast<uint8>(groupCount);
		uint16 body = ParseAlternation();
		ExpectClose(open);
		if (Failed()) {
			return 0;
		}

		uint16 group = AddNode(RegexNodeKind::group, index);
		if (Failed()) {
			return 0;
		}

		nodes[group].child = body;
		return group;
	}


	// position is at the ? after the (. Only (?: is allowed.
	constexpr uint16 ParseSpecialGroup(std::size_t open) {
		std::size_t kind = position + 1;
		if (Is(kind, ':')) {
			position = kind + 1;
			uint16 body = ParseAlternation();
			ExpectClose(open);
			return body;
		}

		if (Is(kind, '=') || Is(kind, '!')) {
			Fail(RegexErrorCode::lookaround, open);
			return 0;
		}

		if (Is(kind, '<')) {
			if (Is(kind + 1, '=') || Is(kind + 1, '!')) {
				Fail(RegexErrorCode::lookaround, open);
			} else {
				Fail(RegexErrorCode::namedGroup, open);
			}

			return 0;
		}

		if (Is(kind, '-') || (kind < pattern.size() && RegexIsFlag(pattern[kind]))) {
			Fail(RegexErrorCode::inlineFlags, open);
			return 0;
		}

		Fail(RegexErrorCode::unknownGroup, open);
		return 0;
	}


	// Reads a digit run, and keeps the value at a cap so a long run still compares above 255.
	constexpr std::size_t ScanNumber(std::size_t& cursor, std::size_t& value) const {
		std::size_t digits = 0;
		value = 0;
		while (cursor < pattern.size() && RegexIsDigit(pattern[cursor])) {
			if (value < 100000) {
				value = value * 10 + static_cast<std::size_t>(pattern[cursor] - '0');
			}

			cursor++;
			digits++;
		}

		return digits;
	}


	// Reads {m}, {m,} or {m,n} at index. Returns the index after the closing brace, or zero when the text is not one of them.
	constexpr std::size_t ScanBraces(std::size_t index, std::size_t& low, std::size_t& high, bool& unbounded) const {
		if (!Is(index, '{')) {
			return 0;
		}

		std::size_t cursor = index + 1;
		if (ScanNumber(cursor, low) == 0) {
			return 0;
		}

		high = low;
		unbounded = false;
		if (Is(cursor, ',')) {
			cursor++;
			if (ScanNumber(cursor, high) == 0) {
				unbounded = true;
			}
		}

		if (!Is(cursor, '}')) {
			return 0;
		}

		return cursor + 1;
	}


	constexpr bool IsBraceAt(std::size_t index) const {
		std::size_t low = 0;
		std::size_t high = 0;
		bool unbounded = false;
		return ScanBraces(index, low, high, unbounded) != 0;
	}


	constexpr bool IsQuantifierAt(std::size_t index) const {
		if (Is(index, '*') || Is(index, '+') || Is(index, '?')) {
			return true;
		}

		return IsBraceAt(index);
	}


	// quantifier: one of * + ? {m} {m,} {m,n}, with one optional lazy ?
	constexpr uint16 ParseQuantifier(uint16 atom, std::size_t groupsBefore) {
		std::size_t offset = position;
		std::size_t low = 0;
		std::size_t high = 0;
		bool unbounded = false;
		if (Is(position, '*')) {
			unbounded = true;
			position++;
		} else if (Is(position, '+')) {
			low = 1;
			unbounded = true;
			position++;
		} else if (Is(position, '?')) {
			high = 1;
			position++;
		} else {
			std::size_t end = ScanBraces(position, low, high, unbounded);
			if (end == 0) {
				return atom;
			}

			position = end;
		}

		if (low > 255 || (!unbounded && high > 255)) {
			Fail(RegexErrorCode::repeatAbove255, offset);
			return 0;
		}

		if (!unbounded && low > high) {
			Fail(RegexErrorCode::repeatMinimumAboveMaximum, offset);
			return 0;
		}

		bool lazy = Is(position, '?');
		if (lazy) {
			position++;
		}

		if (IsQuantifierAt(position)) {
			Fail(RegexErrorCode::nothingToRepeat, position);
			return 0;
		}

		uint16 repeat = AddNode(RegexNodeKind::repeat, 0);
		if (Failed()) {
			return 0;
		}

		nodes[repeat].child = atom;
		nodes[repeat].min = static_cast<uint8>(low);
		nodes[repeat].max = unbounded ? RegexUnbounded : static_cast<uint16>(high);
		nodes[repeat].lazy = lazy;
		nodes[repeat].groupFirst = static_cast<uint8>(groupsBefore + 1);
		nodes[repeat].groupEnd = static_cast<uint8>(groupCount + 1);
		return repeat;
	}


	// Reads the escape at the backslash at position. A shorthand class sets isSet and fills set. Any other escape sets value.
	constexpr void ReadEscape(bool& isSet, RegexClass& set, uint8& value) {
		std::size_t start = position;
		isSet = false;
		if (position + 1 >= pattern.size()) {
			Fail(RegexErrorCode::trailingBackslash, start);
			return;
		}

		char letter = pattern[position + 1];
		position += 2;
		if (letter == 'd' || letter == 'D') {
			RegexFillDigits(set);
			isSet = true;
		} else if (letter == 'w' || letter == 'W') {
			RegexFillWord(set);
			isSet = true;
		} else if (letter == 's' || letter == 'S') {
			RegexFillSpace(set);
			isSet = true;
		} else {
			ReadEscapeByte(letter, start, value);
			return;
		}

		if (letter == 'D' || letter == 'W' || letter == 'S') {
			RegexComplement(set);
		}
	}


	// The escapes that stand for one byte. position is past the letter.
	constexpr void ReadEscapeByte(char letter, std::size_t start, uint8& value) {
		if (letter == 't') {
			value = 9;
		} else if (letter == 'n') {
			value = 10;
		} else if (letter == 'v') {
			value = 11;
		} else if (letter == 'f') {
			value = 12;
		} else if (letter == 'r') {
			value = 13;
		} else if (letter == 'x') {
			if (position + 1 < pattern.size() && RegexIsHex(pattern[position]) && RegexIsHex(pattern[position + 1])) {
				value = static_cast<uint8>(RegexHexValue(pattern[position]) * 16 + RegexHexValue(pattern[position + 1]));
				position += 2;
			} else {
				Fail(RegexErrorCode::hexEscape, start);
			}
		} else if (letter == '0') {
			Fail(RegexErrorCode::escapeNul, start);
		} else if (letter >= '1' && letter <= '9') {
			Fail(RegexErrorCode::backreference, start);
		} else if (letter == 'b' || letter == 'B') {
			Fail(RegexErrorCode::wordBoundary, start);
		} else if (RegexIsPunctuation(letter)) {
			value = static_cast<uint8>(letter);
		} else {
			Fail(RegexErrorCode::unknownEscape, start);
		}
	}


	// An escape outside a class: a shorthand class or one byte.
	constexpr uint16 ParseEscapeAtom() {
		bool isSet = false;
		RegexClass set{};
		uint8 value = 0;
		ReadEscape(isSet, set, value);
		if (Failed()) {
			return 0;
		}

		if (isSet) {
			return AddClassNode(set);
		}

		return AddCharacter(value);
	}


	// One item of a class: a shorthand class, or one byte. Inside a class the bracket is literal only when escaped.
	constexpr void ReadClassItem(bool& isSet, RegexClass& set, uint8& value) {
		if (Is(position, '\\')) {
			ReadEscape(isSet, set, value);
			return;
		}

		isSet = false;
		value = static_cast<uint8>(pattern[position]);
		position++;
	}


	// class: [ ^? items ], where items are bytes, ranges and shorthand escapes
	constexpr uint16 ParseClass() {
		std::size_t open = position;
		position++;
		bool negated = Is(position, '^');
		if (negated) {
			position++;
		}

		if (Is(position, ']')) {
			Fail(RegexErrorCode::emptyClass, open);
			return 0;
		}

		RegexClass bits{};
		while (!Failed() && !Is(position, ']')) {
			if (AtEnd()) {
				Fail(RegexErrorCode::unclosedClass, open);
				return 0;
			}

			std::size_t itemStart = position;
			bool lowIsSet = false;
			RegexClass lowSet{};
			uint8 low = 0;
			ReadClassItem(lowIsSet, lowSet, low);
			if (Failed()) {
				return 0;
			}

			bool isRange = Is(position, '-') && position + 1 < pattern.size() && !Is(position + 1, ']');
			if (!isRange) {
				if (lowIsSet) {
					RegexUnite(bits, lowSet);
				} else {
					RegexSetBit(bits, low);
				}

				continue;
			}

			position++;
			bool highIsSet = false;
			RegexClass highSet{};
			uint8 high = 0;
			ReadClassItem(highIsSet, highSet, high);
			if (Failed()) {
				return 0;
			}

			if (lowIsSet || highIsSet) {
				Fail(RegexErrorCode::classEscapeRange, itemStart);
				return 0;
			}

			if (high < low) {
				Fail(RegexErrorCode::reversedRange, itemStart);
				return 0;
			}

			for (std::size_t value = low; value <= high; value++) {
				RegexSetBit(bits, value);
			}
		}

		if (Failed()) {
			return 0;
		}

		position++;
		if (ignoreCase) {
			RegexFoldCase(bits);
		}

		if (negated) {
			RegexComplement(bits);
		}

		return AddClassNode(bits);
	}


	// The size of a node in instructions. Saturates at RegexCountCap.
	constexpr std::size_t SizeOf(uint16 index) const {
		const RegexNode& node = nodes[index];
		if (node.kind == RegexNodeKind::empty) {
			return 0;
		}

		if (RegexIsLeaf(node.kind)) {
			return 1;
		}

		if (node.kind == RegexNodeKind::group) {
			return RegexCapAdd(2, SizeOf(node.child));
		}

		if (node.kind == RegexNodeKind::concat || node.kind == RegexNodeKind::alternate) {
			std::size_t total = 0;
			std::size_t items = 0;
			for (uint16 item = node.child; item != 0; item = nodes[item].next) {
				total = RegexCapAdd(total, SizeOf(item));
				items++;
			}

			if (node.kind == RegexNodeKind::alternate) {
				total = RegexCapAdd(total, RegexCapMul(2, items - 1));
			}

			return total;
		}

		std::size_t body = RegexCapAdd(SizeOf(node.child), BodyClears(node) ? 1 : 0);
		std::size_t minimum = node.min;
		if (node.max == RegexUnbounded) {
			if (minimum == 0) {
				return RegexCapAdd(body, 2);
			}

			return RegexCapAdd(RegexCapMul(minimum, body), 1);
		}

		std::size_t maximum = node.max;
		return RegexCapAdd(RegexCapMul(minimum, body), RegexCapMul(maximum - minimum, RegexCapAdd(body, 1)));
	}


	// A repeated group clears its captures at the start of each iteration. True when the body holds a group.
	constexpr bool BodyClears(const RegexNode& node) const {
		return node.groupEnd > node.groupFirst;
	}


	// The split of a choice between the body and the exit. A greedy split tries first, a lazy split tries the exit first.
	constexpr RegexInstruction MakeSplit(std::size_t body, std::size_t exit, bool lazy) const {
		std::size_t first = lazy ? exit : body;
		std::size_t second = lazy ? body : exit;
		return RegexInstruction{RegexOperation::split, 0, static_cast<uint16>(first), static_cast<uint16>(second)};
	}


	constexpr void Put(std::size_t index, RegexInstruction instruction) {
		if (index < capacity) {
			output[index] = instruction;
		}
	}


	constexpr void Emit(RegexOperation operation, uint8 arg, uint16 x, uint16 y) {
		Put(emitted, RegexInstruction{operation, arg, x, y});
		emitted++;
	}


	constexpr void EmitNode(uint16 index) {
		const RegexNode& node = nodes[index];
		if (node.kind == RegexNodeKind::empty) {
			return;
		}

		if (node.kind == RegexNodeKind::character) {
			Emit(RegexOperation::character, 0, node.value, 0);
		} else if (node.kind == RegexNodeKind::characterFold) {
			Emit(RegexOperation::characterFold, node.value, 0, 0);
		} else if (node.kind == RegexNodeKind::any) {
			Emit(RegexOperation::any, 0, 0, 0);
		} else if (node.kind == RegexNodeKind::classMatch) {
			Emit(RegexOperation::classMatch, 0, node.value, 0);
		} else if (node.kind == RegexNodeKind::begin) {
			Emit(RegexOperation::begin, 0, 0, 0);
		} else if (node.kind == RegexNodeKind::end) {
			Emit(RegexOperation::end, 0, 0, 0);
		} else if (node.kind == RegexNodeKind::group) {
			Emit(RegexOperation::save, static_cast<uint8>(2 * node.value), 0, 0);
			EmitNode(node.child);
			Emit(RegexOperation::save, static_cast<uint8>(2 * node.value + 1), 0, 0);
		} else if (node.kind == RegexNodeKind::concat) {
			for (uint16 item = node.child; item != 0; item = nodes[item].next) {
				EmitNode(item);
			}
		} else if (node.kind == RegexNodeKind::alternate) {
			EmitAlternatives(node.child);
		} else {
			EmitRepeat(node);
		}
	}


	// The items of an alternation from item on. Each item but the last is guarded by a split, and a jump leads past the rest.
	constexpr void EmitAlternatives(uint16 item) {
		if (nodes[item].next == 0) {
			EmitNode(item);
			return;
		}

		std::size_t split = emitted;
		Emit(RegexOperation::split, 0, 0, 0);
		EmitNode(item);
		std::size_t jump = emitted;
		Emit(RegexOperation::jump, 0, 0, 0);
		std::size_t rest = emitted;
		EmitAlternatives(nodes[item].next);
		Put(split, RegexInstruction{RegexOperation::split, 0, static_cast<uint16>(split + 1), static_cast<uint16>(rest)});
		Put(jump, RegexInstruction{RegexOperation::jump, 0, static_cast<uint16>(emitted), 0});
	}


	// One iteration of a repeat: the clear of the groups inside, then the body.
	constexpr void EmitBody(const RegexNode& node) {
		if (BodyClears(node)) {
			Emit(RegexOperation::clear, static_cast<uint8>(2 * node.groupFirst), static_cast<uint16>(2 * node.groupEnd), 0);
		}

		EmitNode(node.child);
	}


	constexpr void EmitRepeat(const RegexNode& node) {
		std::size_t body = SizeOf(node.child) + (BodyClears(node) ? 1 : 0);
		if (node.max == RegexUnbounded) {
			if (node.min == 0) {
				std::size_t loop = emitted;
				Emit(RegexOperation::split, 0, 0, 0);
				std::size_t bodyStart = emitted;
				EmitBody(node);
				Emit(RegexOperation::jump, 0, static_cast<uint16>(loop), 0);
				std::size_t exit = emitted;
				Put(loop, MakeSplit(bodyStart, exit, node.lazy));
				return;
			}

			for (std::size_t copy = 1; copy < node.min; copy++) {
				EmitBody(node);
			}

			std::size_t loopStart = emitted;
			EmitBody(node);
			std::size_t splitAt = emitted;
			Emit(RegexOperation::split, 0, 0, 0);
			std::size_t exit = emitted;
			Put(splitAt, MakeSplit(loopStart, exit, node.lazy));
			return;
		}

		for (std::size_t copy = 0; copy < node.min; copy++) {
			EmitBody(node);
		}

		std::size_t optional = node.max - node.min;
		std::size_t base = emitted;
		for (std::size_t copy = 0; copy < optional; copy++) {
			Emit(RegexOperation::split, 0, 0, 0);
			EmitBody(node);
		}

		std::size_t exit = emitted;
		for (std::size_t copy = 0; copy < optional; copy++) {
			std::size_t splitAt = base + copy * (body + 1);
			Put(splitAt, MakeSplit(splitAt + 1, exit, node.lazy));
		}
	}
};


// The first pass as a whole: the parse, the count and the limits. A failure is returned in the summary, and the counts are zero.
consteval RegexSummary RegexSummarize(const RegexBuilder& builder) {
	RegexParser parser(builder);
	parser.Parse();
	RegexSummary summary{};
	if (parser.Failed()) {
		summary.failure = parser.GetFailure();
		return summary;
	}

	std::size_t count = parser.Count();
	if (count > RegexMaxInstructions) {
		summary.failure = RegexFailure{RegexErrorCode::instructionLimit, count};
		return summary;
	}

	summary.instructionCount = count;
	summary.classCount = parser.GetClassCount();
	summary.groupCount = parser.GetGroupCount();
	return summary;
}


// The second pass: the arrays of the counted sizes, padded to one element when the count is zero. A parse failure here is not repeated,
// since the first pass has reported it.
template<RegexBuilder (*Make)(), std::size_t InstructionCount, std::size_t ClassCount>
consteval RegexImage<(InstructionCount > 0 ? InstructionCount : 1), (ClassCount > 0 ? ClassCount : 1)> RegexFill() {
	using Image = RegexImage<(InstructionCount > 0 ? InstructionCount : 1), (ClassCount > 0 ? ClassCount : 1)>;
	Image image{};
	RegexBuilder builder = Make();
	RegexParser parser(builder);
	parser.Parse();
	if (parser.Failed()) {
		return image;
	}

	parser.Write(image.code.data(), InstructionCount, image.classes.data(), ClassCount);
	image.failure = parser.GetFailure();
	return image;
}


// Raises the failures of the two passes, and returns the summary when there are none.
template<RegexSummary Value, RegexFailure Filled>
constexpr RegexSummary RegexChecked() {
	RegexRaise<Value.failure.code, Value.failure.value>();
	RegexRaise<Filled.code, Filled.value>();
	return Value;
}


// The compiled program of a pattern. Every member is a compile-time constant. A pattern that breaks a rule is a compile error at the
// first member that is used, and the error names the message and the value.
template<RegexBuilder (*Make)()>
class RegexCompiled {
public:
	static constexpr RegexSummary Summary = RegexSummarize(Make());
	static constexpr std::size_t InstructionSlots = Summary.instructionCount > 0 ? Summary.instructionCount : 1;
	static constexpr std::size_t ClassSlots = Summary.classCount > 0 ? Summary.classCount : 1;
	static constexpr RegexImage<InstructionSlots, ClassSlots> Image = RegexFill<Make, Summary.instructionCount, Summary.classCount>();
	static constexpr RegexSummary Verified = RegexChecked<Summary, Image.failure>();
	// The instruction count, including the final match.
	static constexpr std::size_t Count = Verified.instructionCount;
	static constexpr std::size_t ClassCount = Verified.classCount;
	static constexpr std::size_t GroupCount = Verified.groupCount;
	static constexpr std::size_t SlotCount = 2 * (GroupCount + 1);
	static constexpr std::size_t PlainStackBytes = RegexPlainStackBytes(Count);
	// The closure stack of the captures loop, in frames. It is counted from the program, see RegexCaptureFrames.
	static constexpr std::size_t CaptureFrames = RegexCaptureFrames(Image.code.data(), Count);
	static constexpr std::size_t CapturesStackBytes = RegexCapturesStackBytes(Count, GroupCount, CaptureFrames);
	static constexpr std::size_t StackLimit = Make().StackLimitBytes();
	static constexpr bool IgnoreCase = Make().IsIgnoreCase();
	static constexpr RegexProgramView View{Image.code.data(), Count, Image.classes.data(), SlotCount};
};
