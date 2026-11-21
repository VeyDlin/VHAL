#include "CommandSim.h"
#include "CommandSimSets.h"
#include <Utilities/CommandLine/CommandSet.h>
#include <cstddef>
#include <cstring>
#include <limits>


// The argument declarations as the sets record them: kinds, ranges, defaults, the places of the values in the value area, the
// enum trees, and the defaults written into a value area. The expected offsets follow the layout rule: each value starts at its
// alignment, in declaration order, from the start of the command's value area.


namespace {


// A set with a 64-bit argument, whose range can exceed the int64 metadata.
inline ResultStatus MetaBig(CommandCall<SimApp> call, uint64 number, int64 signedValue) {
	static_cast<void>(call);
	static_cast<void>(number);
	static_cast<void>(signedValue);
	return ResultStatus::ok;
}


using MetaCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("big", "a big number", &MetaBig, Argument<uint64>("n"), Argument<int64>("s").Range(-5, 5).Default(0));
}>;


// Reads the value of type T at offset in a value area.
template<typename T>
T ReadAt(const std::byte* values, std::size_t offset) {
	T value{};
	std::memcpy(&value, values + offset, sizeof(T));
	return value;
}


// Walks text from a node of a level's tree. Returns the node, or CommandNoNode when a character has no child.
template<typename Set>
uint16 WalkFrom(uint16 node, const char* text) {
	for (const char* character = text; *character != '\0' && node != CommandNoNode; character++) {
		node = Set::TreeStep(node, *character);
	}

	return node;
}


}


int main() {
	// Argument counts per command of the top set: key, move, mode, fail, a_b-9.
	CommandExpect(SimCommands::ArgumentCountOf(0) == 1);
	CommandExpect(SimCommands::ArgumentCountOf(1) == 3);
	CommandExpect(SimCommands::ArgumentCountOf(2) == 2);
	CommandExpect(SimCommands::ArgumentCountOf(3) == 0);
	CommandExpect(SimCommands::ArgumentCountOf(4) == 1);

	// Integer metadata: name, kind, range, default flag, and the value offset.
	const CommandArgumentInfo& code = SimCommands::ArgumentInfoOf(0, 0);
	CommandExpect(code.name == "code");
	CommandExpect(code.kind == CommandArgumentKind::Integer);
	CommandExpect(code.minimum == 0);
	CommandExpect(code.maximum == 255);
	CommandExpect(!code.hasDefault);
	CommandExpect(code.valueOffset == 0);

	const CommandArgumentInfo& x = SimCommands::ArgumentInfoOf(1, 0);
	CommandExpect(x.name == "x");
	CommandExpect(x.minimum == -127);
	CommandExpect(x.maximum == 127);
	CommandExpect(x.valueOffset == 0);

	const CommandArgumentInfo& y = SimCommands::ArgumentInfoOf(1, 1);
	CommandExpect(y.name == "y");
	CommandExpect(y.valueOffset == 1);

	// A boolean with a default: its kind, and an offset after the two int8 values.
	const CommandArgumentInfo& fast = SimCommands::ArgumentInfoOf(1, 2);
	CommandExpect(fast.kind == CommandArgumentKind::Boolean);
	CommandExpect(fast.hasDefault);
	CommandExpect(fast.valueOffset == 2);

	// An enum: its kind, a root in the level's nodes, and its value at offset 0. The string after it aligns to 8.
	const CommandArgumentInfo& mode = SimCommands::ArgumentInfoOf(2, 0);
	CommandExpect(mode.kind == CommandArgumentKind::Enumeration);
	CommandExpect(!mode.hasDefault);
	CommandExpect(mode.valueOffset == 0);
	CommandExpect(mode.root != 0);

	const CommandArgumentInfo& label = SimCommands::ArgumentInfoOf(2, 1);
	CommandExpect(label.name == "label");
	CommandExpect(label.kind == CommandArgumentKind::Text);
	CommandExpect(label.hasDefault);
	CommandExpect(label.valueOffset == 8);

	// Value area sizes: the largest command is mode, one byte, then a String<8> of 16 bytes at 8. The group's arguments fit inside.
	static_assert(SimCommands::MaxValueBytes == 24);
	static_assert(SimCommands::MaxValueAlign == 8);
	static_assert(SimCommands::MaxArguments == 3);
	static_assert(SimCommands::MaxSlotBytes > 0);

	// The enum tree of mode: fast and slow are complete names, and their prefixes and other text are not.
	const uint16 root = mode.root;
	CommandExpect(SimCommands::TreePayload(WalkFrom<SimCommands>(root, "fast")) == 0);
	CommandExpect(SimCommands::TreePayload(WalkFrom<SimCommands>(root, "slow")) == 1);
	CommandExpect(SimCommands::TreePayload(WalkFrom<SimCommands>(root, "fa")) == CommandNoNode);
	CommandExpect(WalkFrom<SimCommands>(root, "x") == CommandNoNode);
	CommandExpect(WalkFrom<SimCommands>(root, "fastest") == CommandNoNode);

	// The group's arguments: a range of its own, and a required one in pid.
	const CommandArgumentInfo& rpm = SimMotor::ArgumentInfoOf(0, 0);
	CommandExpect(rpm.name == "rpm");
	CommandExpect(rpm.minimum == -1000);
	CommandExpect(rpm.maximum == 1000);
	CommandExpect(rpm.hasDefault);
	CommandExpect(SimPid::ArgumentInfoOf(0, 0).kind == CommandArgumentKind::Integer);
	CommandExpect(SimPid::ArgumentInfoOf(0, 0).minimum == -1000);
	CommandExpect(!SimPid::ArgumentInfoOf(0, 0).hasDefault);

	// Metadata of 64-bit arguments: the unsigned maximum is clamped to the int64 range, and the signed range is kept.
	const CommandArgumentInfo& big = MetaCommands::ArgumentInfoOf(0, 0);
	CommandExpect(big.minimum == 0);
	CommandExpect(big.maximum == std::numeric_limits<int64>::max());
	CommandExpect(!big.hasDefault);
	const CommandArgumentInfo& signedArgument = MetaCommands::ArgumentInfoOf(0, 1);
	CommandExpect(signedArgument.minimum == -5);
	CommandExpect(signedArgument.maximum == 5);
	CommandExpect(signedArgument.valueOffset == 8);
	static_assert(MetaCommands::MaxValueBytes == 16);

	// Defaults are written into the value area. A required argument is left alone, and an optional one takes its default.
	std::byte values[SimCommands::MaxValueBytes];
	for (std::byte& value : values) {
		value = std::byte{0xAA};
	}

	SimCommands::StoreDefault(0, 0, values);
	CommandExpect(values[0] == std::byte{0xAA});

	SimCommands::StoreDefault(1, 2, values);
	CommandExpect(values[2] == std::byte{0});

	SimCommands::StoreDefault(2, 1, values);
	String<8> defaultLabel = ReadAt<String<8>>(values, 8);
	CommandExpect(defaultLabel.View() == "none");

	// Writing a default for the string leaves the enum's byte alone.
	CommandExpect(values[0] == std::byte{0xAA});

	return CommandSim::Finish("TestCommandArguments");
}
