#pragma once
#include "CommandCheck.h"
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>


// The prefix trees of names: the command and group names of a level, and the value names of an enum argument. Each character moves
// one node index, and no text is kept. A node's children form a linked list through sibling. A node that ends a name holds the
// entry the name leads to in payload.


// The index that means no node, no child and no payload.
constexpr uint16 CommandNoNode = 0xFFFF;

// The longest name an enum value may have. A slot keeps the node index after each character, so the bound sets its size.
constexpr std::size_t CommandMaxNameLength = 32;


struct CommandTreeNode {
	uint16 firstChild = CommandNoNode;
	uint16 sibling = CommandNoNode;
	uint16 payload = CommandNoNode;
	char character = 0;
};


// Name characters are a-z, 0-9, _ and -.
constexpr bool CommandIsNameCharacter(char character) {
	return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
}


// A name must not be empty and must use name characters only.
constexpr void CommandRequireName(std::string_view name) {
	CommandRequire<CommandText{"a name is empty"}>(!name.empty());

	for (char character : name) {
		CommandRequire<CommandText{"a name has a character outside a-z, 0-9, _ and -"}>(CommandIsNameCharacter(character));
	}
}


// One step through a tree: the child of node that holds character, or CommandNoNode when there is none.
constexpr uint16 CommandTreeStep(std::span<const CommandTreeNode> nodes, uint16 node, char character) {
	for (uint16 child = nodes[node].firstChild; child != CommandNoNode; child = nodes[child].sibling) {
		if (nodes[child].character == character) {
			return child;
		}
	}

	return CommandNoNode;
}


// Builds the trees of one level in an array of Capacity nodes. The level copies the used nodes to an array of exact size.
template<std::size_t Capacity>
class CommandTreeBuilder {
public:
	std::array<CommandTreeNode, Capacity> nodes{};
	std::size_t count = 0;


	// Adds a root, which is a tree of its own. The first root of a level is node 0.
	constexpr uint16 AddRoot() {
		return AddNode(0);
	}


	// Stores payload at the end of name below root. Returns false when that end already has a payload, which is a duplicate name.
	constexpr bool Insert(uint16 root, std::string_view name, uint16 payload) {
		uint16 current = root;

		for (char character : name) {
			current = Child(current, character);
		}

		if (nodes[current].payload != CommandNoNode) {
			return false;
		}

		nodes[current].payload = payload;
		return true;
	}


private:
	constexpr uint16 AddNode(char character) {
		CommandRequire<CommandText{"the command set has too many names for the builder"}>(count < Capacity);

		CommandTreeNode node;
		node.character = character;
		nodes[count] = node;
		count++;
		return static_cast<uint16>(count - 1);
	}


	constexpr uint16 Child(uint16 parent, char character) {
		uint16 found = CommandTreeStep(std::span<const CommandTreeNode>(nodes.data(), count), parent, character);
		if (found != CommandNoNode) {
			return found;
		}

		uint16 node = AddNode(character);
		nodes[node].sibling = nodes[parent].firstChild;
		nodes[parent].firstChild = node;
		return node;
	}
};
