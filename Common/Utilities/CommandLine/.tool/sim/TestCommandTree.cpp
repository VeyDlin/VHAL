#include "CommandSim.h"
#include "CommandSimSets.h"
#include <Utilities/CommandLine/CommandSet.h>
#include <cstddef>
#include <string>


// The name trees of the command sets, checked against the names the sets declare. Expected entry numbers follow the declaration
// order: entries are the commands and groups of a level, in the order the chain gives them.


namespace {


// Walks text from the root of a level's tree. Returns the node the text ends at, or CommandNoNode when a character has no child.
template<typename Set>
uint16 WalkName(const char* text) {
	uint16 node = Set::Root;

	for (const char* character = text; *character != '\0' && node != CommandNoNode; character++) {
		node = Set::TreeStep(node, *character);
	}

	return node;
}


// The entry a complete name leads to, or CommandNoNode for a prefix or an unknown name.
template<typename Set>
uint16 EntryOf(const char* text) {
	uint16 node = WalkName<Set>(text);
	return node == CommandNoNode ? CommandNoNode : Set::TreePayload(node);
}


}


// Counts and sizes of the sets, as the chains declare them. These are compile-time facts.
static_assert(SimCommands::CommandCount == 5);
static_assert(SimCommands::GroupCount == 1);
static_assert(SimCommands::EntryCount == 6);
static_assert(SimCommands::HasDefault);
static_assert(SimCommands::HasInterrupt);
static_assert(SimCommands::GroupDepth == 2);
static_assert(SimMotor::CommandCount == 2);
static_assert(SimMotor::GroupCount == 1);
static_assert(!SimMotor::HasDefault);
static_assert(SimPid::CommandCount == 1);
static_assert(SimPid::GroupDepth == 0);
static_assert(SimOnlyDefault::CommandCount == 0);
static_assert(SimOnlyDefault::EntryCount == 0);
static_assert(SimOnlyDefault::HasDefault);
static_assert(!SimOnlyDefault::HasInterrupt);
static_assert(SimOnlyDefault::Level.nodes.size() == 0);
static_assert(SimOnlyDefault::Level.arguments.size() == 0);
static_assert(SimOnlyDefault::MaxValueBytes == 0);
// The level's nodes hold the command tree and the enum tree of mode. The command tree has one node for the root and one for each
// character of the distinct prefixes: k-e-y, m-o (shared), v-e, d-e, t-o-r, f-a-i-l, a-_-b---9, which is 22. The enum tree of mode has a
// root and f-a-s-t and s-l-o-w, which is 9.
static_assert(SimCommands::Level.nodes.size() == 22 + 9);
// The motor level: root, s-p-e-e-d, s-t-o-p (s shared), and p-i-d from the root on its own.
static_assert(SimMotor::Level.nodes.size() == 12);


int main() {
	// Every name of the top set is found, and leads to its entry.
	CommandExpect(EntryOf<SimCommands>("key") == 0);
	CommandExpect(EntryOf<SimCommands>("move") == 1);
	CommandExpect(EntryOf<SimCommands>("mode") == 2);
	CommandExpect(EntryOf<SimCommands>("fail") == 3);
	CommandExpect(EntryOf<SimCommands>("a_b-9") == 4);
	CommandExpect(EntryOf<SimCommands>("motor") == 5);

	// The entry names agree with the names typed.
	CommandExpect(SimCommands::EntryInfo(0).name == "key");
	CommandExpect(SimCommands::EntryInfo(0).isGroup == false);
	CommandExpect(SimCommands::EntryInfo(5).name == "motor");
	CommandExpect(SimCommands::EntryInfo(5).isGroup == true);
	CommandExpect(SimCommands::EntryInfo(5).index == 0);
	CommandExpect(SimCommands::EntryInfo(4).help == "a name with symbols");

	// A prefix that is not a name of its own has no entry, but the walk still finds its node.
	CommandExpect(WalkName<SimCommands>("k") != CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("k") == CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("mo") == CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("mod") == CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("mov") == CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("move") == 1);

	// Text that is no name of the set is refused at the character that breaks it.
	CommandExpect(WalkName<SimCommands>("keys") == CommandNoNode);
	CommandExpect(WalkName<SimCommands>("x") == CommandNoNode);
	CommandExpect(WalkName<SimCommands>("Key") == CommandNoNode);
	CommandExpect(WalkName<SimCommands>("move ") == CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("") == CommandNoNode);

	// Names with symbols and digits are walked the same way.
	CommandExpect(EntryOf<SimCommands>("a_b") == CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("a_b-") == CommandNoNode);
	CommandExpect(EntryOf<SimCommands>("a_b-9") == 4);

	// The group's own tree: its names are found in the group's level, not in the top level.
	CommandExpect(EntryOf<SimMotor>("speed") == 0);
	CommandExpect(EntryOf<SimMotor>("stop") == 1);
	CommandExpect(EntryOf<SimMotor>("pid") == 2);
	CommandExpect(EntryOf<SimMotor>("key") == CommandNoNode);
	CommandExpect(EntryOf<SimPid>("gain") == 0);
	CommandExpect(EntryOf<SimPid>("speed") == CommandNoNode);

	// The default-only set has no name, and walking its (empty) tree is never needed.
	CommandExpect(SimOnlyDefault::EntryCount == 0);

	// The children of the root are listed in declaration order: the first characters of key, move, mode, fail, a_b-9 and motor. move,
	// mode and motor share the m, so the m is listed once, where its first name is declared.
	std::string firstCharacters;

	for (uint16 child = SimCommands::TreeFirstChild(SimCommands::Root); child != CommandNoNode; child = SimCommands::TreeSibling(child)) {
		firstCharacters += SimCommands::TreeCharacter(child);
	}

	CommandExpect(firstCharacters == "kmfa");

	// The children of the m node are move, mode and motor, in the order they were declared.
	uint16 mo = WalkName<SimCommands>("mo");
	std::string afterMo;

	for (uint16 child = SimCommands::TreeFirstChild(mo); child != CommandNoNode; child = SimCommands::TreeSibling(child)) {
		afterMo += SimCommands::TreeCharacter(child);
	}

	CommandExpect(afterMo == "vdt");

	return CommandSim::Finish("TestCommandTree");
}
