#include <cstdio>
#include <string>
#include <vector>

#include "StatusAnswer.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


// The status answer as the composite prints it: the echo, the three status lines, and the prompt.
const std::string StatusAnswerText =
	"status\r\n"
	"state=Configured addr=2 numLock=false capsLock=true scrollLock=false\r\n"
	"baud=57600 data=8 parity=none stop=1\r\n"
	"dtr=true looped=12 cdcDropped=0 usbErr=3\r\n"
	"> ";


void CheckCompleteness() {
	Expect(AnswerComplete(StatusAnswerText), "an answer with its prompt is complete");
	Expect(!AnswerComplete("status\r\nstate=Configured"), "an answer without the prompt is not complete");
	Expect(!AnswerComplete("status\r\n>"), "a prompt without its space is not complete");
	Expect(!AnswerComplete("> "), "a prompt with no line before it is not complete");
}


void CheckSplitting() {
	std::vector<std::string> lines = SplitLines("ok\r\n> ");
	Expect(lines.size() == 2 && lines[0] == "ok" && lines[1] == "> ", "CR LF ends a line and the prompt is the last line");

	std::vector<std::string> bare = SplitLines("a\nb\rc\r\n");
	Expect(bare.size() == 3 && bare[0] == "a" && bare[1] == "b" && bare[2] == "c", "LF and CR end lines, and a pair ends one");

	std::vector<std::string> blank = SplitLines("a\r\n\r\nb\r\n");
	Expect(blank.size() == 3 && blank[1].empty(), "an empty line between two CR LF pairs is kept");

	std::vector<std::string> doubled = SplitLines("a\r\r\nb");
	Expect(doubled.size() == 3 && doubled[1].empty(), "CR then CR LF is two line ends");
}


void CheckCommandBody() {
	std::vector<std::string> body;
	std::string error;
	Expect(CommandBody("key f13\r\nok\r\n> ", "key f13", body, error), "a key answer parses");
	Expect(body.size() == 1 && body[0] == "ok", "the body of a key answer is its ok line");

	Expect(CommandBody("> key f13\r\nok\r\n> ", "key f13", body, error), "an echo that follows the prompt on its line parses");
	Expect(body.size() == 1 && body[0] == "ok", "the body after a prompted echo is its ok line");

	Expect(
		CommandBody("USB composite demo. Type help.\r\n> key f13\r\nok\r\n> ", "key f13", body, error),
		"a greeting before the echo is not part of the answer");
	Expect(body.size() == 1 && body[0] == "ok", "the body skips the greeting");

	Expect(!CommandBody("key f13\r\nok\r\n> ", "key f14", body, error), "a wrong echo is refused");
	Expect(error.find("did not echo") != std::string::npos, "the wrong echo is named");

	Expect(!CommandBody("key f13\r\nok\r\n", "key f13", body, error), "an answer without a prompt is refused");
	Expect(error.find("no prompt") != std::string::npos, "the missing prompt is named");

	Expect(!CommandBody("> ", "status", body, error), "an answer too short to hold an echo is refused");
}


void CheckStatusParsing() {
	std::vector<std::string> body;
	std::string error;
	Expect(CommandBody(StatusAnswerText, "status", body, error), "the status answer has a body");

	CompositeStatus status;
	Expect(ParseStatus(body, status, error), "the status fields parse");
	Expect(status.state == "Configured", "the state is read");
	Expect(status.address == 2, "the address is read");
	Expect(!status.numLock && status.capsLock && !status.scrollLock, "the keyboard LEDs are read");
	Expect(status.coding.baud == 57600, "the baud rate is read");
	Expect(status.coding.dataBits == 8, "the data bits are read");
	Expect(status.coding.parity == "none", "the parity name is read");
	Expect(status.coding.stopBits == "1", "the stop bits are read");
	Expect(status.dtr, "DTR is read as true");
	Expect(status.looped == 12, "the looped count is read");
	Expect(status.cdcDropped == 0, "the dropped count is read");
	Expect(status.usbErrors == 3, "the USB error count is read");

	std::vector<std::string> missing = { "state=Configured addr=2" };
	CompositeStatus partial;
	Expect(!ParseStatus(missing, partial, error), "a status with missing fields is refused");
	Expect(error.find("no parity field") != std::string::npos, "the missing field is named");

	std::vector<std::string> badNumber = {
		"state=Configured addr=2 numLock=false capsLock=false scrollLock=false",
		"baud=5x7600 data=8 parity=none stop=1",
		"dtr=false looped=0 cdcDropped=0 usbErr=0"
	};
	Expect(!ParseStatus(badNumber, partial, error), "a non-number field is refused");
	Expect(error.find("baud") != std::string::npos, "the malformed field is named");
}


void CheckLineCoding() {
	LineCoding reported;
	reported.baud = 57600;
	reported.dataBits = 8;
	reported.parity = "none";
	reported.stopBits = "1";

	LineCoding expected = reported;
	Expect(SameLineCoding(reported, expected), "the same coding matches");

	expected.parity = "even";
	Expect(!SameLineCoding(reported, expected), "a different parity does not match");

	Expect(DescribeLineCoding(reported) == "57600 8N1", "57600 8N1 is described in shorthand");

	LineCoding second;
	second.baud = 9600;
	second.dataBits = 7;
	second.parity = "even";
	second.stopBits = "1";
	Expect(DescribeLineCoding(second) == "9600 7E1", "9600 7E1 is described in shorthand");

	LineCoding markStop;
	markStop.baud = 19200;
	markStop.dataBits = 8;
	markStop.parity = "odd";
	markStop.stopBits = "1.5";
	Expect(DescribeLineCoding(markStop) == "19200 8O1.5", "odd parity and 1.5 stop bits are described");
}


}


int main() {
	CheckCompleteness();
	CheckSplitting();
	CheckCommandBody();
	CheckStatusParsing();
	CheckLineCoding();

	std::printf("usbclasscheck status answer test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
