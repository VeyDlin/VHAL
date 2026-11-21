#pragma once
#include "CommandArgument.h"
#include "CommandLevel.h"
#include "CommandNameTree.h"
#include "CommandPack.h"
#include "CommandTypes.h"
#include <Utilities/Console/Print.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>


// The Help module. It prints the commands and groups of the set, from their declarations, for three kinds of line:
//
//   help                 the top level
//   help <path>          the level at a path of group and command names, for example: help motor pid gain
//   <path> -h            the same, with the path before the last word, for example: motor -h
//
// The word help is a help request only when no command of the set is named help. A path ends at its first command. Words after that
// command are ignored. A word that names nothing at its level prints: unknown command 'xyz', try help.
//
// The output is:
//   a level:    the group's help text, when it is a group, then one line per entry: its name, and its help text, marked (group)
//   a command:  its help text, a usage line with the path and <required> or [optional] arguments, then one line per argument with its
//               kind, its range or values, and its default
//
// Help keeps its own copy of the line, up to LineSize characters, because the console keeps none. A line that is longer is not a
// help request. Help takes the line it answers: the console raises no events for it, and no command runs.
//
// Help must be listed before any module that takes characters or events. A module listed earlier that takes a character hides it
// from Help, so Help's copy of the line would differ from the console's.
//
// A set with no commands and no groups cannot have help, and a listed Help is a compile error for it.
//
//   using App = CommandLine<AppCommands, AppContext, Help>;


// The next word of text from position, or an empty view when the text has no more words.
inline std::string_view CommandHelpWord(std::string_view text, std::size_t& position) {
	while (position < text.size() && text[position] == ' ') {
		position++;
	}

	std::size_t start = position;

	while (position < text.size() && text[position] != ' ') {
		position++;
	}

	return text.substr(start, position - start);
}


// Writes count spaces.
inline void CommandHelpPad(Print& out, std::size_t count) {
	for (std::size_t index = 0; index < count; index++) {
		out.Write(" ");
	}
}


// Writes an argument's kind, with its range or values, and its default. A custom argument type has no range and no default text.
template<typename Declaration>
void CommandHelpDescribe(Print& out, const Declaration& declaration) {
	if constexpr (Declaration::Kind == CommandArgumentKind::Integer) {
		out.Write("number ");
		out.Write(declaration.minimum);
		out.Write("..");
		out.Write(declaration.maximum);

		if (declaration.hasDefault) {
			out.Write(", default ");
			out.Write(declaration.defaultValue);
		}
	} else if constexpr (Declaration::Kind == CommandArgumentKind::Boolean) {
		out.Write("on or off");

		if (declaration.hasDefault) {
			out.Write(", default ");
			out.Write(declaration.defaultValue ? "on" : "off");
		}
	} else if constexpr (Declaration::Kind == CommandArgumentKind::Enumeration) {
		out.Write("one of ");

		for (std::size_t index = 0; index < declaration.values.size(); index++) {
			if (index > 0) {
				out.Write(", ");
			}

			out.Write(declaration.values[index].name);
		}

		if (declaration.hasDefault) {
			for (const CommandEnumValue<typename Declaration::Value>& item : declaration.values) {
				if (item.value == declaration.defaultValue) {
					out.Write(", default ");
					out.Write(item.name);
				}
			}
		}
	} else if constexpr (Declaration::Kind == CommandArgumentKind::Text) {
		out.Write("text of at most ");
		out.Write(Declaration::MaxLength);
		out.Write(" characters");

		if (declaration.hasDefault) {
			out.Write(", default \"");
			out.Write(declaration.defaultValue.View());
			out.Write("\"");
		}
	} else {
		out.Write("value");
	}
}


// Writes an argument as it appears in a usage line: <name> when required, [name] when it has a default.
template<typename Declaration>
void CommandHelpUsage(Print& out, const Declaration& declaration) {
	out.Write(declaration.hasDefault ? "[" : "<");
	out.Write(declaration.name);
	out.Write(declaration.hasDefault ? "]" : ">");
}


// Writes one command: its name and help text, its usage line with the path before it, and one line per argument.
struct CommandHelpCommandPage {
	Print& out;
	const std::string_view* names;
	std::size_t depth;


	template<typename Item>
	void operator()(const Item& item) {
		out.Write(item.name);
		out.Write(": ");
		out.Write(item.help);
		out.Line();

		out.Write("usage: ");

		for (std::size_t index = 0; index < depth; index++) {
			out.Write(names[index]);
			out.Write(" ");
		}

		out.Write(item.name);

		std::size_t width = 0;

		CommandPackForEach(item.arguments, [&](const auto& declaration) {
			out.Write(" ");
			CommandHelpUsage(out, declaration);
			width = std::max(width, declaration.name.size());
		});

		out.Line();

		CommandPackForEach(item.arguments, [&](const auto& declaration) {
			out.Write("  ");
			out.Write(declaration.name);
			CommandHelpPad(out, width - declaration.name.size() + 2);
			CommandHelpDescribe(out, declaration);
			out.Line();
		});
	}
};


// Calls the command page for the command at the level it is in.
struct CommandHelpCommandVisit {
	CommandHelpCommandPage& page;
	std::size_t command;


	template<typename LevelSet>
	void operator()() {
		LevelSet::VisitCommand(command, page);
	}
};


// Writes one level: the group's name and help text when group is set, then one line per entry in declaration order.
struct CommandHelpLevelPage {
	Print& out;
	const CommandEntryInfo* group;


	template<typename LevelSet>
	void operator()() {
		if (group != nullptr) {
			out.Write(group->name);
			out.Write(": ");
			out.Write(group->help);
			out.Line();
		}

		std::size_t width = 0;

		for (std::size_t index = 0; index < LevelSet::EntryCount; index++) {
			width = std::max(width, LevelSet::EntryInfo(index).name.size());
		}

		for (std::size_t index = 0; index < LevelSet::EntryCount; index++) {
			const CommandEntryInfo& entry = LevelSet::EntryInfo(index);

			out.Write("  ");
			out.Write(entry.name);
			CommandHelpPad(out, width - entry.name.size() + 2);

			if (entry.isGroup) {
				out.Write("(group) ");
			}

			out.Write(entry.help);
			out.Line();
		}
	}
};


// Finds a word among the names of a level. found is the entry the word names, or null when it names none.
struct CommandHelpLookup {
	std::string_view word;
	const CommandEntryInfo* found = nullptr;


	template<typename LevelSet>
	void operator()() {
		uint16 node = LevelSet::Root;

		for (char character : word) {
			node = LevelSet::TreeStep(node, character);

			if (node == CommandNoNode) {
				return;
			}
		}

		uint16 entry = LevelSet::TreePayload(node);

		if (entry != CommandNoNode) {
			found = &LevelSet::EntryInfo(entry);
		}
	}
};


struct Help {
private:
	static constexpr std::size_t LineSize = 64;

	std::array<char, LineSize> line{};
	std::size_t length = 0;
	bool lost = false;


public:
	// Keeps the characters the console reads, and removes one for each Backspace, as the console does.
	template<typename Console>
	bool OnCharacter(Console& console, char character) {
		static_cast<void>(console);

		if (lost) {
			return false;
		}

		if (character == 0x08 || character == 0x7F) {
			if (length > 0) {
				length--;
			}

			return false;
		}

		if (character == '\t') {
			character = ' ';
		}

		if (character < 0x20 || character > 0x7E) {
			return false;
		}

		if (length < LineSize) {
			line[length] = character;
			length++;
		} else {
			lost = true;
		}

		return false;
	}


	// Lost input, or a new terminal state, makes the copy of the line wrong, so the copy is dropped.
	template<typename Console>
	bool OnEvent(Console& console, const CommandEvent& event) {
		static_cast<void>(console);

		switch (event.kind) {
			case CommandEventKind::Interrupt:
			case CommandEventKind::TerminalOpened:
			case CommandEventKind::TerminalClosed:
				Clear();
				break;
			case CommandEventKind::InputOverflow:
				lost = true;
				break;
			default:
				break;
		}

		return false;
	}


	// Answers a help request. Returns true for a help request, and the console then raises no events for the line.
	template<typename Console>
	bool OnLineEnd(Console& console) {
		using Commands = typename Console::Commands;
		static_assert(Commands::EntryCount > 0, "Help needs a command or a group in the set");

		bool answered = false;

		if (!lost) {
			answered = Answer<Commands>(console.Output());
		}

		Clear();
		return answered;
	}


private:
	void Clear() {
		length = 0;
		lost = false;
	}


	// Reads the words of the line. A help request is help followed by a path, or a path followed by -h.
	template<typename Commands>
	bool Answer(Print& out) {
		std::string_view text(line.data(), length);

		std::size_t scan = 0;
		std::string_view first = CommandHelpWord(text, scan);

		if (first.empty()) {
			return false;
		}

		std::size_t count = 1;
		std::string_view last = first;

		for (std::string_view word = CommandHelpWord(text, scan); !word.empty(); word = CommandHelpWord(text, scan)) {
			count++;
			last = word;
		}

		std::array<uint16, Commands::GroupDepth + 1> groups{};
		std::array<std::string_view, Commands::GroupDepth + 1> names{};

		CommandHelpLookup top{first};
		Commands::VisitLevel(groups.data(), 0, top);

		std::size_t skip = 0;
		std::size_t limit = 0;

		if (first == "help" && top.found == nullptr) {
			// A trailing -h is a request, so it is not a word of the path after help.
			skip = 1;
			limit = last == "-h" ? count - 1 : count;
		} else if (count >= 2 && last == "-h") {
			limit = count - 1;
		} else {
			return false;
		}

		std::size_t depth = 0;
		std::size_t command = 0;
		bool named = false;
		const CommandEntryInfo* found = nullptr;

		std::size_t walk = 0;

		for (std::size_t index = 0; index < limit && !named; index++) {
			std::string_view word = CommandHelpWord(text, walk);

			if (index < skip) {
				continue;
			}

			CommandHelpLookup lookup{word};
			Commands::VisitLevel(groups.data(), depth, lookup);

			if (lookup.found == nullptr) {
				out.Write("unknown command '");
				out.Write(word);
				out.Write("', try help");
				out.Line();
				return true;
			}

			found = lookup.found;

			if (found->isGroup) {
				groups[depth] = found->index;
				names[depth] = found->name;
				depth++;
			} else {
				command = found->index;
				named = true;
			}
		}

		if (named) {
			CommandHelpCommandPage page{out, names.data(), depth};
			CommandHelpCommandVisit visit{page, command};
			Commands::VisitLevel(groups.data(), depth, visit);
		} else {
			CommandHelpLevelPage page{out, depth > 0 ? found : nullptr};
			Commands::VisitLevel(groups.data(), depth, page);
		}

		return true;
	}
};
