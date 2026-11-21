#include "CommandSim.h"
#include <Utilities/CommandLine/CommandFloatArguments.h>
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <cstddef>
#include <string>


// The FloatArguments module, fed one byte at a time. The float and double arguments are read by FloatParser, and their ranges are checked
// at Enter. A float range in a message is written with two decimals. The expected texts are written from the rules in
// CommandFloatArguments.h. The set has no Default(), so the standard texts are printed.


struct FloatApp {
	double gain = -1;
	float rpm = -99;
};


ResultStatus Speed(CommandCall<FloatApp> call, double gain, float rpm) {
	call.app.gain = gain;
	call.app.rpm = rpm;
	return ResultStatus::ok;
}


using FloatSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &Speed, Argument<double>("gain").Range(0.0, 10.0), Argument<float>("rpm").Range(-1.5f, 2.5f).Default(0.5f));
}>;


ResultStatus Free(CommandCall<FloatApp> call, double value) {
	call.app.gain = value;
	return ResultStatus::ok;
}


// A double with no Range: its range is the type's finite range.
using FreeSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("free", "a double with no range", &Free, Argument<double>("value"));
}>;


ResultStatus FreeFloat(CommandCall<FloatApp> call, float value) {
	call.app.rpm = value;
	return ResultStatus::ok;
}


// A float with no Range: its range is the type's finite range too.
using FreeFloatSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("freef", "a float with no range", &FreeFloat, Argument<float>("value"));
}>;


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


int main() {
	// Numbers are read as typed: a double, and a float with an exponent.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 3 1.25\r");

		CommandExpect(out.Text() == "speed 3 1.25\r\n> ");
		CommandExpect(app.gain == 3.0);
		CommandExpect(app.rpm == 1.25f);
	}

	// The optional float takes its default when it is not typed.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 3\r");

		CommandExpect(out.Text() == "speed 3\r\n> ");
		CommandExpect(app.rpm == 0.5f);
	}

	// A sign and an exponent are read; the range is checked on the value.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 2 -1e0\r");

		CommandExpect(out.Text() == "speed 2 -1e0\r\n> ");
		CommandExpect(app.gain == 2.0);
		CommandExpect(app.rpm == -1.0f);
	}

	// A double above its range is refused at Enter, with the range in the message.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 11\r");

		CommandExpect(out.Text() == "speed 11\r\nspeed: gain must be 0.00..10.00\r\n> ");
		CommandExpect(app.gain == -1);
	}

	// A float below its range is refused, with its own range.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 1 3\r");

		CommandExpect(out.Text() == "speed 1 3\r\nspeed: rpm must be -1.50..2.50\r\n> ");
	}

	// Nan is read, and the range check refuses it, since no comparison with nan holds.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed nan\r");

		CommandExpect(out.Text() == "speed nan\r\nspeed: gain must be 0.00..10.00\r\n> ");
	}

	// Infinity is read and refused as out of range, both as a double and as a float, at either sign. Only finite values are accepted.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed inf\r");

		CommandExpect(out.Text() == "speed inf\r\nspeed: gain must be 0.00..10.00\r\n> ");
		CommandExpect(app.gain == -1);
	}

	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed -inf\r");

		CommandExpect(out.Text() == "speed -inf\r\nspeed: gain must be 0.00..10.00\r\n> ");
		CommandExpect(app.gain == -1);
	}

	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 1 inf\r");

		CommandExpect(out.Text() == "speed 1 inf\r\nspeed: rpm must be -1.50..2.50\r\n> ");
		CommandExpect(app.rpm == -99);
	}

	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 1 -inf\r");

		CommandExpect(out.Text() == "speed 1 -inf\r\nspeed: rpm must be -1.50..2.50\r\n> ");
		CommandExpect(app.rpm == -99);
	}

	// Nan is refused as out of range in both arguments, as it is above.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed 1 nan\r");

		CommandExpect(out.Text() == "speed 1 nan\r\nspeed: rpm must be -1.50..2.50\r\n> ");
		CommandExpect(app.rpm == -99);
	}

	// With no Range, the range is the type's finite range, so infinity and nan are refused there too, with the whole range in the message.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FreeSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "free inf\r");

		CommandExpect(out.Text() == "free inf\r\nfree: value must be -1.80e+308..1.80e+308\r\n> ");
		CommandExpect(app.gain == -1);
	}

	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FreeSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "free -inf\r");

		CommandExpect(out.Text() == "free -inf\r\nfree: value must be -1.80e+308..1.80e+308\r\n> ");
		CommandExpect(app.gain == -1);
	}

	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FreeSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "free nan\r");

		CommandExpect(out.Text() == "free nan\r\nfree: value must be -1.80e+308..1.80e+308\r\n> ");
		CommandExpect(app.gain == -1);
	}

	// A float with no Range: infinity and nan are refused with the float's finite range, which is narrower than a double's.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FreeFloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "freef inf\r");

		CommandExpect(out.Text() == "freef inf\r\nfreef: value must be -3.40e+38..3.40e+38\r\n> ");
		CommandExpect(app.rpm == -99);
	}

	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FreeFloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "freef -inf\r");

		CommandExpect(out.Text() == "freef -inf\r\nfreef: value must be -3.40e+38..3.40e+38\r\n> ");
		CommandExpect(app.rpm == -99);
	}

	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FreeFloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "freef nan\r");

		CommandExpect(out.Text() == "freef nan\r\nfreef: value must be -3.40e+38..3.40e+38\r\n> ");
		CommandExpect(app.rpm == -99);
	}

	// A finite value with no Range is read.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FreeSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "free 2.5\r");

		CommandExpect(out.Text() == "free 2.5\r\n> ");
		CommandExpect(app.gain == 2.5);
	}

	// A character that is not part of a number is refused as it is typed, and reported at Enter as not a number.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed x\r");

		CommandExpect(out.Text() == "speed x\r\nspeed: gain must be a number\r\n> ");
	}

	// A required float that is not typed is missing.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, FloatArguments> console(app, out);
		Type(console, "speed\r");

		CommandExpect(out.Text() == "speed\r\nspeed: gain is missing\r\n> ");
	}

	// Help prints the range and the default with two decimals.
	{
		CommandSim::RecordingPrint out;
		FloatApp app;
		CommandLine<FloatSet, FloatApp, Help, FloatArguments> console(app, out);
		Type(console, "help speed\r");

		CommandExpect(out.Text() ==
			"help speed\r\n"
			"speed: set the speed\r\n"
			"usage: speed <gain> [rpm]\r\n"
			"  gain  number 0.00..10.00\r\n"
			"  rpm   number -1.50..2.50, default 0.50\r\n"
			"> ");
	}

	return CommandSim::Finish("TestCommandFloatArguments");
}
