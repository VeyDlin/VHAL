#pragma once
#include <Utilities/DataTypes.h>


// Turns the bytes of the input into what the console acts on: a character, a line end, or a key from an escape sequence. Escape
// sequences that name no key are dropped here, so an arrow key that no module uses injects nothing. A line end is CR, LF, CR LF or LF
// CR: the first byte ends a line and its opposite right after it is part of the same line end. Two CRs, or two LFs, end two lines.

// The byte of Ctrl+C.
inline constexpr uint8 CommandInterruptByte = 0x03;


// The keys an escape sequence can name. Other is a sequence with no key of its own.
enum class CommandKey : uint8 {
	Up,
	Down,
	Left,
	Right,
	Home,
	End,
	Delete,
	Other
};


enum class CommandInputKind : uint8 {
	Ignore,
	Character,
	LineEnd,
	Key
};


struct CommandInputStep {
	CommandInputKind kind = CommandInputKind::Ignore;
	uint8 byte = 0;
	CommandKey key = CommandKey::Other;
};


class CommandInput {
private:
	enum class Escape : uint8 {
		None,
		Start,
		Csi,
		Ss3
	};


	Escape escape = Escape::None;
	uint8 lineEnd = 0;
	uint16 csiNumber = 0;


public:
	// Reads one byte of input.
	CommandInputStep Step(uint8 byte) {
		uint8 previousLineEnd = lineEnd;
		lineEnd = 0;

		CommandInputStep step;

		if (escape != Escape::None) {
			if (byte >= 0x20) {
				return Sequence(byte);
			}

			// A control byte breaks the sequence, and is read as input.
			escape = Escape::None;
		}

		if (byte == 0x1B) {
			escape = Escape::Start;
			return step;
		}

		if (byte == '\r' || byte == '\n') {
			if (previousLineEnd != 0 && previousLineEnd != byte) {
				return step;
			}

			lineEnd = byte;
			step.kind = CommandInputKind::LineEnd;
			return step;
		}

		step.kind = CommandInputKind::Character;
		step.byte = byte;
		return step;
	}


	void Reset() {
		escape = Escape::None;
		lineEnd = 0;
		csiNumber = 0;
	}


private:
	// The byte after an escape, or inside one. Bytes of a sequence that name no key are ignored.
	CommandInputStep Sequence(uint8 byte) {
		CommandInputStep step;

		if (escape == Escape::Start) {
			if (byte == '[') {
				escape = Escape::Csi;
				csiNumber = 0;
			} else if (byte == 'O') {
				escape = Escape::Ss3;
			} else {
				escape = Escape::None;
			}

			return step;
		}

		if (escape == Escape::Csi) {
			if (byte >= '0' && byte <= '9') {
				if (csiNumber < 1000) {
					csiNumber = static_cast<uint16>(csiNumber * 10 + (byte - '0'));
				}

				return step;
			}

			if (byte >= 0x40 && byte <= 0x7E) {
				escape = Escape::None;
				step.kind = CommandInputKind::Key;
				step.key = CsiKey(byte);
			}

			return step;
		}

		escape = Escape::None;
		step.kind = CommandInputKind::Key;
		step.key = SsKey(byte);
		return step;
	}


	CommandKey CsiKey(uint8 final) const {
		if (final == '~') {
			if (csiNumber == 1 || csiNumber == 7) {
				return CommandKey::Home;
			}

			if (csiNumber == 3) {
				return CommandKey::Delete;
			}

			if (csiNumber == 4 || csiNumber == 8) {
				return CommandKey::End;
			}

			return CommandKey::Other;
		}

		return SsKey(final);
	}


	static CommandKey SsKey(uint8 final) {
		if (final == 'A') {
			return CommandKey::Up;
		}

		if (final == 'B') {
			return CommandKey::Down;
		}

		if (final == 'C') {
			return CommandKey::Right;
		}

		if (final == 'D') {
			return CommandKey::Left;
		}

		if (final == 'H') {
			return CommandKey::Home;
		}

		if (final == 'F') {
			return CommandKey::End;
		}

		return CommandKey::Other;
	}
};
