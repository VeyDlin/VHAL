#pragma once
#include <Utilities/CommandLine/CommandSet.h>
#include <string>


// The command sets the host tests share. Three levels: the application's set, a motor group, and a pid group inside the motor
// group. Each handler takes the context its own level declares, and the application type derives from all of them.


enum class SimMode : uint8 {
	Fast,
	Slow
};


struct PidControl {
	int gain = 0;
};


struct MotorControl : PidControl {
	int rpm = 0;
	int stops = 0;
};


struct SimApp : MotorControl {
	int keyCode = -1;
	int moveX = 0;
	int moveY = 0;
	bool moveFast = true;
	SimMode mode = SimMode::Fast;
	String<8> label{};
	int interrupts = 0;
	int defaults = 0;
	int defaultKind = -1;
	std::string defaultText;
};


inline ResultStatus SimKey(CommandCall<SimApp> call, uint8 code) {
	call.app.keyCode = code;
	return ResultStatus::ok;
}


inline ResultStatus SimMove(CommandCall<SimApp> call, int8 x, int8 y, bool fast) {
	call.app.moveX = x;
	call.app.moveY = y;
	call.app.moveFast = fast;
	return ResultStatus::ok;
}


inline ResultStatus SimSetMode(CommandCall<SimApp> call, SimMode mode, String<8> label) {
	call.app.mode = mode;
	call.app.label = label;
	return ResultStatus::ok;
}


inline ResultStatus SimFail(CommandCall<SimApp> call) {
	static_cast<void>(call);
	return ResultStatus::invalidParameter;
}


inline ResultStatus SimSpeed(CommandCall<MotorControl> call, int16 rpm) {
	call.app.rpm = rpm;
	return ResultStatus::ok;
}


inline ResultStatus SimStop(CommandCall<MotorControl> call) {
	call.app.stops++;
	return ResultStatus::ok;
}


inline ResultStatus SimGain(CommandCall<PidControl> call, int32 value) {
	call.app.gain = value;
	return ResultStatus::ok;
}


inline void SimOnEvent(CommandCall<SimApp> call, const CommandEvent& event) {
	call.app.defaults++;
	call.app.defaultKind = static_cast<int>(event.kind);
	call.app.defaultText = std::string(event.text);
}


inline void SimOnInterrupt(SimApp& app) {
	app.interrupts++;
}


using SimPid = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("gain", "set the gain", &SimGain, Argument<int32>("value").Range(-1000, 1000));
}>;


using SimMotor = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &SimSpeed, Argument<int16>("rpm").Range(-1000, 1000).Default(0))
		.Command("stop", "stop the motor", &SimStop)
		.Group<SimPid>("pid", "pid tuning");
}>;


using SimCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &SimKey, Argument<uint8>("code"))
		.Command("move", "move the pointer", &SimMove, Argument<int8>("x").Range(-127, 127), Argument<int8>("y").Range(-127, 127), Argument<bool>("fast").Default(false))
		.Command("mode", "set a mode", &SimSetMode, Argument<SimMode>("mode").Values({{"fast", SimMode::Fast}, {"slow", SimMode::Slow}}), Argument<String<8>>("label").Default("none"))
		.Command("fail", "always fails", &SimFail)
		.Command("a_b-9", "a name with symbols", &SimKey, Argument<uint8>("code"))
		.Group<SimMotor>("motor", "motor control")
		.OnInterrupt(&SimOnInterrupt)
		.Default(&SimOnEvent);
}>;


using SimOnlyDefault = CommandSet<+[] {
	return CommandSetBuilder().Default(&SimOnEvent);
}>;
