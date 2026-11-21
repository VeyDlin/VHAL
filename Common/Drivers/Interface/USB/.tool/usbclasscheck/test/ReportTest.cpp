#include <cstdio>
#include <string>
#include <vector>

#include "ExitCodes.h"
#include "Report.h"

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


void CheckVerdicts() {
	CheckReport passing("passing");
	passing.AddStep("one", StepStatus::Pass, "");
	passing.AddStep("two", StepStatus::Pass, "");
	Expect(passing.Verdict() == StepStatus::Pass, "all passing steps give a pass");

	CheckReport failing("failing");
	failing.AddStep("one", StepStatus::Pass, "");
	failing.AddStep("two", StepStatus::Fail, "");
	Expect(failing.Verdict() == StepStatus::Fail, "a failed step gives a fail");

	CheckReport errored("errored");
	errored.AddStep("one", StepStatus::Fail, "");
	errored.AddStep("two", StepStatus::Error, "");
	Expect(errored.Verdict() == StepStatus::Error, "an error outranks a fail");

	CheckReport empty("empty");
	Expect(empty.Verdict() == StepStatus::Pass, "a check with no steps passes");
	Expect(empty.Name() == "empty", "the name is kept");
	Expect(errored.Steps().size() == 2, "the steps are kept in order");
}


void CheckExitCodes() {
	CheckReport passing("cdc");
	passing.AddStep("one", StepStatus::Pass, "");
	CheckReport failing("hid");
	failing.AddStep("one", StepStatus::Fail, "");
	CheckReport errored("vendor");
	errored.AddStep("one", StepStatus::Error, "");

	std::vector<CheckReport> allPass = { passing };
	Expect(ExitCodeOf(allPass) == ExitOk, "all pass exits 0");

	std::vector<CheckReport> someFail = { passing, failing };
	Expect(ExitCodeOf(someFail) == ExitFailed, "a failed check exits 3");

	std::vector<CheckReport> someError = { failing, errored };
	Expect(ExitCodeOf(someError) == ExitDevice, "a check that could not run exits 1, outranking 3");

	std::vector<CheckReport> none;
	Expect(ExitCodeOf(none) == ExitOk, "no checks exits 0");
}


void CheckText() {
	CheckReport report("cdc");
	report.AddStep("COM port", StepStatus::Pass, "COM11");
	report.AddStep("line coding", StepStatus::Fail, "the device reports 9600");
	std::vector<CheckReport> reports = { report };

	std::string text = FormatReports(reports);
	Expect(text.find("  PASS  COM port: COM11\n") != std::string::npos, "a passing step is printed with its detail");
	Expect(text.find("  FAIL  line coding: the device reports 9600\n") != std::string::npos, "a failing step is printed with its detail");
	Expect(text.find("cdc: FAIL\n") != std::string::npos, "the check verdict is printed");
	Expect(text.find("Result: FAIL: cdc\n") != std::string::npos, "the result names the failed check");

	CheckReport errored("hid");
	errored.AddStep("Raw Input", StepStatus::Error, "no device");
	std::vector<CheckReport> erroredRun = { errored };
	Expect(FormatReports(erroredRun).find("Result: ERROR, a check could not run: hid\n") != std::string::npos, "an error result is named");

	CheckReport good("storage");
	good.AddStep("volume", StepStatus::Pass, "");
	std::vector<CheckReport> goodRun = { good };
	Expect(FormatReports(goodRun).find("Result: PASS (1 checks)\n") != std::string::npos, "a passing run prints PASS");
}


}


int main() {
	CheckVerdicts();
	CheckExitCodes();
	CheckText();

	std::printf("usbclasscheck report test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
