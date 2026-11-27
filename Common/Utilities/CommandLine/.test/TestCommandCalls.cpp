#include "CommandSim.h"
#include "CommandSimSets.h"
#include <Utilities/CommandLine/CommandSet.h>
#include <cstddef>
#include <cstring>


// The handlers of the sets, called with values put in the value area by hand (the parser comes with the console). Each test checks
// what the handler did to the application, the status it returned, the default and the interrupt handlers, and the group paths.


namespace {


// Writes a value of type T at offset in a value area.
template<typename T>
void WriteAt(std::byte* values, std::size_t offset, T value) {
	std::memcpy(values + offset, &value, sizeof(T));
}


// Calls a set's command at a group path, as the console does: the visitor gets the typed set at the end of the path.
struct RecordingVisitor {
	std::size_t commandCount = 0;
	std::size_t groupCount = 0;
	std::size_t groupDepth = 0;
	bool visited = false;
};


template<typename LevelSet>
void Describe(RecordingVisitor& visitor) {
	visitor.visited = true;
	visitor.commandCount = LevelSet::CommandCount;
	visitor.groupCount = LevelSet::GroupCount;
	visitor.groupDepth = LevelSet::GroupDepth;
}


// Describes the set the visitor is called with.
struct TopVisitor {
	RecordingVisitor* record = nullptr;


	template<typename LevelSet>
	void operator()() {
		Describe<LevelSet>(*record);
	}
};


// Calls one command of the set it is given.
struct CommandCaller {
	SimApp* app = nullptr;
	Print* out = nullptr;
	const std::byte* values = nullptr;
	std::size_t command = 0;
	ResultStatus status = ResultStatus::notFound;

	template<typename LevelSet>
	void operator()() {
		status = LevelSet::Invoke(command, *app, *out, values);
	}
};


}


int main() {
	CommandSim::RecordingPrint out;
	SimApp app;

	alignas(SimCommands::MaxValueAlign) std::byte values[SimCommands::MaxValueBytes];
	for (std::byte& value : values) {
		value = std::byte{0};
	}

	// key: one uint8 at offset 0.
	WriteAt<uint8>(values, 0, 65);
	CommandExpect(SimCommands::Invoke(0, app, out, values) == ResultStatus::ok);
	CommandExpect(app.keyCode == 65);

	// move: two int8 at offsets 0 and 1, a bool at 2. The negative value keeps its sign through the value area.
	WriteAt<int8>(values, 0, -5);
	WriteAt<int8>(values, 1, 7);
	WriteAt<bool>(values, 2, true);
	CommandExpect(SimCommands::Invoke(1, app, out, values) == ResultStatus::ok);
	CommandExpect(app.moveX == -5);
	CommandExpect(app.moveY == 7);
	CommandExpect(app.moveFast == true);

	// The default of fast is written first, and the handler then sees false.
	SimCommands::StoreDefault(1, 2, values);
	CommandExpect(SimCommands::Invoke(1, app, out, values) == ResultStatus::ok);
	CommandExpect(app.moveFast == false);

	// mode: the enum value 1 (slow) at offset 0, and a String<8> with abc at offset 8.
	WriteAt<uint8>(values, 0, 1);
	String<8> typed;
	typed.characters[0] = 'a';
	typed.characters[1] = 'b';
	typed.characters[2] = 'c';
	typed.length = 3;
	WriteAt<String<8>>(values, 8, typed);
	CommandExpect(SimCommands::Invoke(2, app, out, values) == ResultStatus::ok);
	CommandExpect(app.mode == SimMode::Slow);
	CommandExpect(app.label.View() == "abc");

	// The default label, "none", is what the handler gets when the argument is omitted.
	SimCommands::StoreDefault(2, 1, values);
	CommandExpect(SimCommands::Invoke(2, app, out, values) == ResultStatus::ok);
	CommandExpect(app.label.View() == "none");

	// fail returns the status its handler gives.
	CommandExpect(SimCommands::Invoke(3, app, out, values) == ResultStatus::invalidParameter);

	// A name with symbols reaches its own handler, and the number of commands counts it.
	WriteAt<uint8>(values, 0, 9);
	CommandExpect(SimCommands::Invoke(4, app, out, values) == ResultStatus::ok);
	CommandExpect(app.keyCode == 9);

	// A command index past the commands is not found, and no handler runs.
	app.keyCode = -1;
	CommandExpect(SimCommands::Invoke(5, app, out, values) == ResultStatus::notFound);
	CommandExpect(SimCommands::Invoke(99, app, out, values) == ResultStatus::notFound);
	CommandExpect(app.keyCode == -1);

	// The group motor is reached by its path. speed is its command 0, with an int16 at offset 0 of the value area.
	CommandCaller speed;
	speed.app = &app;
	speed.out = &out;
	speed.values = values;
	speed.command = 0;
	WriteAt<int16>(values, 0, -300);
	uint16 motorPath[1] = {0};
	SimCommands::VisitLevel(motorPath, 1, speed);
	CommandExpect(speed.status == ResultStatus::ok);
	CommandExpect(app.rpm == -300);

	// stop is motor's command 1: it takes no values.
	CommandCaller stop;
	stop.app = &app;
	stop.out = &out;
	stop.values = values;
	stop.command = 1;
	SimCommands::VisitLevel(motorPath, 1, stop);
	CommandExpect(stop.status == ResultStatus::ok);
	CommandExpect(app.stops == 1);

	// The pid group sits two levels down. Its gain is an int32 at offset 0, and it writes the gain of the same application.
	CommandCaller gain;
	gain.app = &app;
	gain.out = &out;
	gain.values = values;
	gain.command = 0;
	WriteAt<int32>(values, 0, 250);
	uint16 pidPath[2] = {0, 0};
	SimCommands::VisitLevel(pidPath, 2, gain);
	CommandExpect(gain.status == ResultStatus::ok);
	CommandExpect(app.gain == 250);

	// The visitor sees the typed set at each path: the top set with an empty path, the motor group and the pid group below it.
	RecordingVisitor top;
	TopVisitor topVisitor{&top};
	SimCommands::VisitLevel(nullptr, 0, topVisitor);
	CommandExpect(top.visited);
	CommandExpect(top.commandCount == 5);
	CommandExpect(top.groupCount == 1);
	CommandExpect(top.groupDepth == 2);

	RecordingVisitor motor;
	TopVisitor motorVisitor{&motor};
	SimCommands::VisitLevel(motorPath, 1, motorVisitor);
	CommandExpect(motor.commandCount == 2);
	CommandExpect(motor.groupCount == 1);

	RecordingVisitor pid;
	TopVisitor pidVisitor{&pid};
	SimCommands::VisitLevel(pidPath, 2, pidVisitor);
	CommandExpect(pid.commandCount == 1);
	CommandExpect(pid.groupCount == 0);

	// The default handler gets the event, and runs against the application. A set without one does nothing.
	CommandEvent word;
	word.kind = CommandEventKind::Word;
	word.text = "xyz";
	SimCommands::CallDefault(app, out, word);
	CommandExpect(app.defaults == 1);
	CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::Word));
	CommandExpect(app.defaultText == "xyz");

	SimOnlyDefault::CallDefault(app, out, word);
	CommandExpect(app.defaults == 2);

	// The interrupt handler runs at once when called, and a set without one has no such call to make.
	SimCommands::CallInterrupt(app);
	CommandExpect(app.interrupts == 1);
	static_assert(SimCommands::HasInterrupt);
	static_assert(!SimMotor::HasInterrupt);

	// Every handler's context is the application type or one of its bases, checked at compile time.
	SimCommands::CheckContext<SimApp>();

	// The reader of an argument is placed in the storage the runtime gives. A slot gets its characters through Push and its value
	// from Finish. This is the sanity check that the placement works; the reader is tested with the parser.
	alignas(SimCommands::MaxSlotAlign) std::byte storage[SimCommands::MaxSlotBytes];
	CommandSlot* slot = SimCommands::CreateSlot(0, 0, storage);
	CommandExpect(slot != nullptr);
	if (slot != nullptr) {
		CommandExpect(slot->Push('6'));
		CommandExpect(slot->Push('5'));
		std::byte value[1] = {std::byte{0}};
		CommandExpect(slot->Finish(value) == CommandFault::None);
		CommandExpect(value[0] == std::byte{65});
	}

	CommandExpect(SimCommands::CreateSlot(0, 9, storage) == nullptr);

	return CommandSim::Finish("TestCommandCalls");
}
