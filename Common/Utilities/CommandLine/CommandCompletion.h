#pragma once
#include "CommandLineEditing.h"
#include "CommandModules.h"
#include "CommandNameTree.h"
#include "CommandParser.h"
#include "CommandTypes.h"
#include <Utilities/Console/Print.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>


// The completion module. Tab completes the token at the end of the line from the name tree of the command set: a command or group name,
// or an enum value. It needs no text of its own:
//
//   using App = CommandLine<AppCommands, AppContext, Help, LineEditing<64>, Completion>;
//
// A unique continuation is typed in. When several names continue the token, the part they share is typed in, and a second Tab (with no
// change to the line since the first) lists the choices in declaration order, on a new line, then writes the prompt and the line again.
// With the cursor before the end of the line, Tab first moves the cursor to the end. Tab works with and without LineEditing.
//
// Without LineEditing the console keeps no text of the line, so there is nothing to write again. The second Tab then rings the bell (0x07)
// and lists nothing. The parser still holds the line, so the next keys continue it.
//
// Tab is taken only when the set has commands or groups. A set with a default handler only types the Tab as a space.


// The step a Tab takes once the token is known.
enum class CompletionStep : uint8 {
	Nothing,
	List,
	Type,
	TypeAndArm
};


// The characters of the tree from node down to a complete name, with the path from the root in text from position depth. Returns true
// when node is found below the node the search starts at.
template<typename L>
bool CompletionFind(uint16 node, uint16 target, char* text, std::size_t& depth) {
	if (node == target) {
		return true;
	}

	for (uint16 child = L::TreeFirstChild(node); child != CommandNoNode; child = L::TreeSibling(child)) {
		text[depth] = L::TreeCharacter(child);
		depth++;

		if (CompletionFind<L>(child, target, text, depth)) {
			return true;
		}

		depth--;
	}

	return false;
}


// The node of the subtree at node that holds payload, or CommandNoNode when none does.
template<typename L>
uint16 CompletionNodeOf(uint16 node, uint16 payload) {
	if (L::TreePayload(node) == payload) {
		return node;
	}

	for (uint16 child = L::TreeFirstChild(node); child != CommandNoNode; child = L::TreeSibling(child)) {
		uint16 found = CompletionNodeOf<L>(child, payload);

		if (found != CommandNoNode) {
			return found;
		}
	}

	return CommandNoNode;
}


// The smallest payload at or above floor in the subtree at node, or CommandNoNode when none is. Payloads are declaration indexes, so
// taking them in order lists the choices as they were declared.
template<typename L>
uint16 CompletionNextPayload(uint16 node, uint16 floor) {
	uint16 best = CommandNoNode;

	if (L::TreePayload(node) != CommandNoNode && L::TreePayload(node) >= floor) {
		best = L::TreePayload(node);
	}

	for (uint16 child = L::TreeFirstChild(node); child != CommandNoNode; child = L::TreeSibling(child)) {
		uint16 found = CompletionNextPayload<L>(child, floor);

		if (found != CommandNoNode && (best == CommandNoNode || found < best)) {
			best = found;
		}
	}

	return best;
}


struct Completion {
private:
	bool armed = false;
	uint32 armedChanges = 0;


public:
	template<typename Console>
	bool OnCharacter(Console& console, char character) {
		if (character != '\t') {
			return false;
		}

		if constexpr (Console::Commands::EntryCount == 0) {
			return false;
		} else {
			Complete(console);
			return true;
		}
	}


private:
	template<typename Console>
	void Complete(Console& console) {
		using Commands = typename Console::Commands;
		constexpr std::size_t Capacity = std::max(Commands::MaxNameLength, CommandMaxNameLength);

		bool buffered = console.template WithModule<CommandLineBufferFeature>([&]<typename Editor>(Editor& editor) {
			editor.ToEnd(console);
		});

		CommandEditPoint point = console.Parsed().EditPoint();
		std::array<char, Capacity> suffix{};
		std::size_t suffixLength = 0;
		CompletionStep step = CompletionStep::Nothing;
		bool bell = false;

		console.Parsed().WithLevel([&]<typename L>() {
			if (point.spot == CommandSpot::None) {
				return;
			}

			if (point.slot != nullptr && !L::EnumNode(point.command, point.argument, point.slot, point.node)) {
				return;
			}

			uint16 end = point.node;

			while (L::TreePayload(end) == CommandNoNode && L::TreeFirstChild(end) != CommandNoNode && L::TreeSibling(L::TreeFirstChild(end)) == CommandNoNode) {
				end = L::TreeFirstChild(end);
				suffix[suffixLength] = L::TreeCharacter(end);
				suffixLength++;
			}

			if (L::TreeFirstChild(end) == CommandNoNode) {
				if (L::TreePayload(end) != CommandNoNode) {
					step = CompletionStep::Type;
				}

				return;
			}

			if (armed && console.Changes() == armedChanges) {
				if (buffered) {
					step = CompletionStep::List;
					ListChoices<L>(console, point);
				} else {
					bell = true;
				}

				return;
			}

			step = CompletionStep::TypeAndArm;
		});

		// A bell keeps the arming, so the next Tab with no change to the line rings again rather than typing.
		if (bell) {
			console.Output().Write("\x07");
			return;
		}

		armed = step == CompletionStep::TypeAndArm;

		if (step == CompletionStep::Type || step == CompletionStep::TypeAndArm) {
			for (std::size_t index = 0; index < suffixLength; index++) {
				console.Type(suffix[index]);
			}
		}

		if (armed) {
			armedChanges = console.Changes();
		}
	}


	// Writes the names that continue the token, in declaration order, on a new line, then the prompt and the line.
	template<typename L, typename Console>
	void ListChoices(Console& console, const CommandEditPoint& point) {
		constexpr std::size_t Capacity = std::max(Console::Commands::MaxNameLength, CommandMaxNameLength);
		std::array<char, Capacity> text{};

		Print& out = console.Output();
		out.Line();

		bool first = true;

		uint16 payload = CompletionNextPayload<L>(point.node, 0);

		while (payload != CommandNoNode) {
			std::size_t depth = 0;
			CompletionFind<L>(point.root, CompletionNodeOf<L>(point.node, payload), text.data(), depth);

			if (!first) {
				out.Write("  ");
			}

			first = false;
			out.Write(std::string_view(text.data(), depth));

			uint16 floor = static_cast<uint16>(payload + 1);
			payload = CompletionNextPayload<L>(point.node, floor);
		}

		out.Line();

		console.RedrawPrompt();

		console.template WithModule<CommandLineBufferFeature>([&]<typename Editor>(Editor& editor) {
			console.Echo(editor.Text());
		});
	}
};
