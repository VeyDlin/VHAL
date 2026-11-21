#pragma once
#include "CommandInput.h"
#include "CommandModules.h"
#include "CommandTypes.h"
#include <Utilities/Console/Print.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>


// The line editing module. It keeps the line in a buffer of Size bytes, with a cursor, so the keys can move in it and change it:
//
//   Left, Right             move the cursor one character
//   Home, Ctrl+A            move the cursor to the start of the line
//   End, Ctrl+E             move the cursor to the end of the line
//   Delete                  delete the character at the cursor
//   Backspace               delete the character before the cursor
//   Ctrl+K                  delete from the cursor to the end of the line
//   Ctrl+W                  delete the word before the cursor
//   a printable character   insert it at the cursor; when the buffer is full, the character is refused with a bell
//
//   using App = CommandLine<AppCommands, AppContext, Help, LineEditing<64>>;
//
// Typing at the end of the line goes to the parser one character at a time, as it does without the module. An edit before the end (an
// insert, a delete, or a Ctrl+K or Ctrl+W that leaves a tail) changes the buffer, redraws the rest of the line, and replays the whole
// buffer through the parser, so the parser holds the line as the screen shows it. On the screen the cursor moves with CSI n D and
// CSI n C, and the rest of the line is cleared with CSI K.
//
// The buffer is cleared when the prompt is written after a line end, and when the terminal opens or closes or Ctrl+C cancels the line.
// Help and History read the line at the line end, before the prompt, so they see the whole line.
//
// Tab is not taken here. Completion takes it. A Tab that no module takes is typed as a space.


template<std::size_t Size>
class LineEditing {
	static_assert(Size > 0, "a line editor needs a buffer of at least one byte");

public:
	using Provides = CommandLineBufferFeature;


private:
	std::array<char, Size> buffer{};
	std::size_t length = 0;
	std::size_t cursor = 0;
	bool ended = false;


public:
	// The line as it stands.
	std::string_view Text() const {
		return std::string_view(buffer.data(), length);
	}


	// Moves the cursor to the end of the line. Completion does this first, so that it types at the end.
	template<typename Console>
	void ToEnd(Console& console) {
		MoveRight(console.Output(), length - cursor);
		cursor = length;
	}


	// Writes the line again at the current position of the screen cursor, which the console has just moved to the start of the line, and
	// moves the cursor back to the place it has in the buffer. Ansi uses it after it clears the screen.
	template<typename Console>
	void Redraw(Console& console) {
		console.Echo(Text());
		MoveLeft(console.Output(), length - cursor);
	}


	// Replaces the line with text, up to Size characters, and redraws it with the cursor at the end. The text is replayed through the
	// parser. History recalls its lines with this.
	template<typename Console>
	void Replace(Console& console, std::string_view text) {
		Print& out = console.Output();
		MoveLeft(out, cursor);

		length = std::min(text.size(), Size);
		std::copy_n(text.data(), length, buffer.data());
		cursor = length;

		console.Replay(buffer.data(), length);
		console.Echo(Text());
		out.Write("\x1b[K");
	}


	template<typename Console>
	bool OnCharacter(Console& console, char character) {
		uint8 byte = static_cast<uint8>(character);

		if (byte == 0x08 || byte == 0x7F) {
			Backspace(console);
			return true;
		}

		if (byte == 0x01) {
			ToStart(console.Output());
			return true;
		}

		if (byte == 0x05) {
			ToEnd(console);
			return true;
		}

		if (byte == 0x0B) {
			DeleteToEnd(console);
			return true;
		}

		if (byte == 0x17) {
			DeleteWord(console);
			return true;
		}

		if (byte >= 0x20 && byte <= 0x7E) {
			Insert(console, character);
			return true;
		}

		return false;
	}


	template<typename Console>
	bool OnKey(Console& console, CommandKey key) {
		Print& out = console.Output();

		switch (key) {
			case CommandKey::Left:
				if (cursor > 0) {
					MoveLeft(out, 1);
					cursor--;
				}

				return true;
			case CommandKey::Right:
				if (cursor < length) {
					MoveRight(out, 1);
					cursor++;
				}

				return true;
			case CommandKey::Home:
				ToStart(out);
				return true;
			case CommandKey::End:
				ToEnd(console);
				return true;
			case CommandKey::Delete:
				DeleteAt(console);
				return true;
			default:
				return false;
		}
	}


	// The line is over. Its buffer stays until the prompt, so that the modules that run at the line end can read it.
	template<typename Console>
	bool OnLineEnd(Console& console) {
		static_cast<void>(console);
		ended = true;
		return false;
	}


	template<typename Console>
	bool OnEvent(Console& console, const CommandEvent& event) {
		static_cast<void>(console);

		if (event.kind == CommandEventKind::Interrupt || event.kind == CommandEventKind::TerminalOpened || event.kind == CommandEventKind::TerminalClosed) {
			Clear();
		}

		return false;
	}


	template<typename Console>
	void OnPrompt(Console& console) {
		static_cast<void>(console);

		if (ended) {
			Clear();
		}
	}


private:
	void Clear() {
		length = 0;
		cursor = 0;
		ended = false;
	}


	static void MoveLeft(Print& out, std::size_t count) {
		if (count > 0) {
			out.Write("\x1b[");
			out.Write(count);
			out.Write("D");
		}
	}


	static void MoveRight(Print& out, std::size_t count) {
		if (count > 0) {
			out.Write("\x1b[");
			out.Write(count);
			out.Write("C");
		}
	}


	void ToStart(Print& out) {
		MoveLeft(out, cursor);
		cursor = 0;
	}


	// Removes count characters from position, and moves the rest of the line down over them.
	void Remove(std::size_t position, std::size_t count) {
		for (std::size_t index = position + count; index < length; index++) {
			buffer[index - count] = buffer[index];
		}

		length -= count;
	}


	// Redraws the line from from, after an edit. The cursor is at before, and it ends at after. The buffer is replayed first, so the
	// parser knows whether the edited line holds a secret before the tail is drawn.
	template<typename Console>
	void Edit(Console& console, std::size_t from, std::size_t before, std::size_t after) {
		Print& out = console.Output();
		console.Replay(buffer.data(), length);
		MoveLeft(out, before - from);

		if (length > from) {
			console.Echo(std::string_view(buffer.data() + from, length - from));
		}

		out.Write("\x1b[K");
		MoveLeft(out, length - after);
		cursor = after;
	}


	template<typename Console>
	void Insert(Console& console, char character) {
		if (length == Size) {
			console.Output().Write("\x07");
			return;
		}

		if (cursor == length) {
			buffer[length] = character;
			length++;
			cursor = length;
			console.Append(character);
			return;
		}

		std::size_t position = cursor;

		for (std::size_t index = length; index > position; index--) {
			buffer[index] = buffer[index - 1];
		}

		buffer[position] = character;
		length++;
		Edit(console, position, position, position + 1);
	}


	// At the end, the parser takes the erase, as it does for typed text. Before the end, the character before the cursor is removed.
	template<typename Console>
	void Backspace(Console& console) {
		if (cursor == 0) {
			return;
		}

		if (cursor == length) {
			if (console.EraseLast()) {
				length--;
				cursor = length;
			}

			return;
		}

		std::size_t before = cursor;
		Remove(before - 1, 1);
		Edit(console, before - 1, before, before - 1);
	}


	template<typename Console>
	void DeleteAt(Console& console) {
		if (cursor == length) {
			return;
		}

		Remove(cursor, 1);
		Edit(console, cursor, cursor, cursor);
	}


	template<typename Console>
	void DeleteToEnd(Console& console) {
		if (cursor == length) {
			return;
		}

		length = cursor;
		Edit(console, cursor, cursor, cursor);
	}


	// Removes the spaces and then the word before the cursor. At the end, each character goes through the parser's erase.
	template<typename Console>
	void DeleteWord(Console& console) {
		std::size_t start = cursor;

		while (start > 0 && buffer[start - 1] == ' ') {
			start--;
		}

		while (start > 0 && buffer[start - 1] != ' ') {
			start--;
		}

		if (start == cursor) {
			return;
		}

		if (cursor == length) {
			while (cursor > start) {
				if (!console.EraseLast()) {
					break;
				}

				length--;
				cursor--;
			}

			return;
		}

		std::size_t before = cursor;
		Remove(start, before - start);
		cursor = start;
		Edit(console, start, before, start);
	}
};
