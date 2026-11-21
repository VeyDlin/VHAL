#pragma once
#include "CommandInput.h"
#include "CommandModules.h"
#include "CommandTypes.h"
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <string_view>


// The history module. The up and down keys recall the lines the console read before. A recalled line is put in the line buffer of
// LineEditing and replayed through the parser, so it can be edited and entered again:
//
//   using App = CommandLine<AppCommands, AppContext, Help, LineEditing<64>, History<4, 128>>;
//
// Lines is the most lines held, and Bytes is the size of the ring they are kept in. A line takes its length plus two bytes. A line longer
// than 255 characters is not kept. Empty lines and a line that repeats the newest one are not kept either.
//
// The first Up keeps the line being typed as the newest entry (the draft), so that Down can bring it back. While a recall lasts, the draft
// takes one of the Lines. It leaves the ring when Down passes the newest line, when the line is entered, or when Ctrl+C or a terminal change
// ends the line. Up does nothing when the draft cannot be kept: when the line is longer than 255 characters or than the ring, or when
// keeping it would drop the only older line.
//
// History needs LineEditing. Without it, the console does not compile.


template<std::size_t Lines, std::size_t Bytes>
class History {
	static_assert(Lines >= 2, "a history needs at least two lines: one to recall and one for the draft");
	static_assert(Bytes > 2, "a history needs room for a line and its two lengths");

public:
	using Needs = CommandLineBufferFeature;


private:
	static constexpr std::size_t MaxLength = 255;

	std::array<char, Bytes> ring{};
	std::size_t first = 0;
	std::size_t used = 0;
	std::size_t count = 0;
	std::size_t shown = 0;


public:
	// Up and Down recall lines. A key that is not one of them is not taken.
	template<typename Console>
	bool OnKey(Console& console, CommandKey key) {
		if (key == CommandKey::Up) {
			Older(console);
			return true;
		}

		if (key == CommandKey::Down) {
			Newer(console);
			return true;
		}

		return false;
	}


	// Keeps the line the user entered. The draft leaves first, so that the line is the one the user entered.
	template<typename Console>
	bool OnLineEnd(Console& console) {
		std::string_view text = CurrentLine(console);

		if (shown > 0) {
			DropNewest();
			shown = 0;
		}

		// A line that holds a secret is not kept.
		if (!text.empty() && !console.HoldsSecret() && (count == 0 || !Matches(0, text))) {
			Push(text, false);
		}

		return false;
	}


	// Ctrl+C and a terminal change end the line, so the draft of a recall goes with it.
	template<typename Console>
	bool OnEvent(Console& console, const CommandEvent& event) {
		static_cast<void>(console);

		if (event.kind == CommandEventKind::Interrupt || event.kind == CommandEventKind::TerminalOpened || event.kind == CommandEventKind::TerminalClosed) {
			if (shown > 0) {
				DropNewest();
				shown = 0;
			}
		}

		return false;
	}


private:
	// The text of the line buffer, read through LineEditing.
	template<typename Console>
	static std::string_view CurrentLine(Console& console) {
		std::string_view text;

		console.template WithModule<CommandLineBufferFeature>([&]<typename Editor>(Editor& editor) {
			text = editor.Text();
		});

		return text;
	}


	// Puts the line being typed in the ring as the newest entry, and shows the newest line.
	template<typename Console>
	void Older(Console& console) {
		if (shown > 0) {
			if (shown + 1 < count) {
				shown++;
				Recall(console, shown);
			}

			return;
		}

		if (count == 0) {
			return;
		}

		// The draft cannot be kept when it holds a secret, and Up does nothing when keeping it would drop the only older line.
		if (console.HoldsSecret() || !Push(CurrentLine(console), true)) {
			return;
		}

		shown = 1;
		Recall(console, shown);
	}


	// Shows the next newer line. Past the newest one, the draft comes back, and leaves the ring.
	template<typename Console>
	void Newer(Console& console) {
		if (shown == 0) {
			return;
		}

		if (shown > 1) {
			shown--;
			Recall(console, shown);
			return;
		}

		Recall(console, 0);
		DropNewest();
		shown = 0;
	}


	// Copies the entry back steps from the newest into a buffer of the line editor, and replays it there.
	template<typename Console>
	void Recall(Console& console, std::size_t back) {
		std::array<char, MaxLength> line{};
		std::size_t size = Read(back, line.data());

		console.template WithModule<CommandLineBufferFeature>([&]<typename Editor>(Editor& editor) {
			editor.Replace(console, std::string_view(line.data(), size));
		});
	}


	// The length of the entry that ends at end.
	std::size_t LengthBefore(std::size_t end) const {
		return static_cast<std::size_t>(static_cast<uint8>(ring[(end + Bytes - 1) % Bytes]));
	}


	// The offset of the first byte of the entry back steps from the newest. An entry is its length, its characters and its length again.
	std::size_t EntryStart(std::size_t back) const {
		std::size_t end = (first + used) % Bytes;
		std::size_t start = end;

		for (std::size_t index = 0; index <= back; index++) {
			start = (end + Bytes - (LengthBefore(end) + 2)) % Bytes;
			end = start;
		}

		return start;
	}


	std::size_t Read(std::size_t back, char* out) const {
		std::size_t start = EntryStart(back);
		std::size_t size = static_cast<std::size_t>(static_cast<uint8>(ring[start]));

		for (std::size_t index = 0; index < size; index++) {
			out[index] = ring[(start + 1 + index) % Bytes];
		}

		return size;
	}


	bool Matches(std::size_t back, std::string_view text) const {
		std::size_t start = EntryStart(back);

		if (static_cast<std::size_t>(static_cast<uint8>(ring[start])) != text.size()) {
			return false;
		}

		for (std::size_t index = 0; index < text.size(); index++) {
			if (ring[(start + 1 + index) % Bytes] != text[index]) {
				return false;
			}
		}

		return true;
	}


	// Adds the text as the newest entry. The oldest entries leave while the ring is full. Returns false when the text cannot be kept, or, with
	// keepOlder, when the entries that would leave include the last one.
	bool Push(std::string_view text, bool keepOlder) {
		std::size_t need = text.size() + 2;

		if (text.size() > MaxLength || need > Bytes) {
			return false;
		}

		if (keepOlder && !OlderSurvive(need)) {
			return false;
		}

		while (count >= Lines || used + need > Bytes) {
			DropOldest();
		}

		std::size_t start = (first + used) % Bytes;
		ring[start] = static_cast<char>(text.size());

		for (std::size_t index = 0; index < text.size(); index++) {
			ring[(start + 1 + index) % Bytes] = text[index];
		}

		ring[(start + 1 + text.size()) % Bytes] = static_cast<char>(text.size());
		used += need;
		count++;
		return true;
	}


	// Whether at least one entry is left when the oldest entries leave to make room for need bytes, as Push does.
	bool OlderSurvive(std::size_t need) const {
		std::size_t left = count;
		std::size_t held = used;
		std::size_t position = first;

		while (left >= Lines || held + need > Bytes) {
			if (left == 1) {
				return false;
			}

			std::size_t size = static_cast<std::size_t>(static_cast<uint8>(ring[position])) + 2;
			position = (position + size) % Bytes;
			held -= size;
			left--;
		}

		return left > 0;
	}


	void DropOldest() {
		std::size_t need = static_cast<std::size_t>(static_cast<uint8>(ring[first])) + 2;
		first = (first + need) % Bytes;
		used -= need;
		count--;
	}


	void DropNewest() {
		std::size_t need = LengthBefore((first + used) % Bytes) + 2;
		used -= need;
		count--;
	}
};
