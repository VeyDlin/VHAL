#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class StepStatus : std::uint8_t {
	Pass,
	Fail,
	// The step could not run: no board, no port, the device did not answer, a tool is missing.
	Error
};

// One step of a check: what it looked at, its result and a short detail.
struct CheckStep {
	std::string name;
	StepStatus status = StepStatus::Pass;
	std::string detail;
};

// The steps of one check, in the order they ran. The verdict is the worst step: Error (the check could not run) outranks
// Fail (the board answered and the answer is wrong).
class CheckReport {
private:
	std::string name;
	std::vector<CheckStep> steps;


public:
	explicit CheckReport(const std::string& checkName);

	void AddStep(const std::string& stepName, StepStatus status, const std::string& detail);

	const std::string& Name() const;

	const std::vector<CheckStep>& Steps() const;

	StepStatus Verdict() const;
};


const char* StepStatusText(StepStatus status);

// 0 when every check passed, 1 when a check could not run, 3 when a check failed. Error outranks Fail.
int ExitCodeOf(const std::vector<CheckReport>& reports);

// Every step of every check, one line each, then a verdict line per check and the result line.
std::string FormatReports(const std::vector<CheckReport>& reports);
