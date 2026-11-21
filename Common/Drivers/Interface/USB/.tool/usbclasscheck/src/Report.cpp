#include "Report.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ExitCodes.h"

CheckReport::CheckReport(const std::string& checkName) {
	name = checkName;
}


void CheckReport::AddStep(const std::string& stepName, StepStatus status, const std::string& detail) {
	CheckStep step;
	step.name = stepName;
	step.status = status;
	step.detail = detail;
	steps.push_back(step);
}


const std::string& CheckReport::Name() const {
	return name;
}


const std::vector<CheckStep>& CheckReport::Steps() const {
	return steps;
}


StepStatus CheckReport::Verdict() const {
	StepStatus verdict = StepStatus::Pass;
	for (const CheckStep& step : steps) {
		if (step.status == StepStatus::Error) {
			return StepStatus::Error;
		}

		if (step.status == StepStatus::Fail) {
			verdict = StepStatus::Fail;
		}
	}

	return verdict;
}


const char* StepStatusText(StepStatus status) {
	switch (status) {
		case StepStatus::Pass:
			return "PASS";
		case StepStatus::Fail:
			return "FAIL";
		case StepStatus::Error:
			return "ERROR";
	}

	return "ERROR";
}


int ExitCodeOf(const std::vector<CheckReport>& reports) {
	int exitCode = ExitOk;
	for (const CheckReport& report : reports) {
		StepStatus verdict = report.Verdict();
		if (verdict == StepStatus::Error) {
			return ExitDevice;
		}

		if (verdict == StepStatus::Fail) {
			exitCode = ExitFailed;
		}
	}

	return exitCode;
}


std::string FormatReports(const std::vector<CheckReport>& reports) {
	std::string text;
	std::string notPassed;
	for (const CheckReport& report : reports) {
		for (const CheckStep& step : report.Steps()) {
			text += "  ";
			text += StepStatusText(step.status);
			text += "  ";
			text += step.name;
			if (!step.detail.empty()) {
				text += ": " + step.detail;
			}

			text += "\n";
		}

		StepStatus verdict = report.Verdict();
		text += report.Name() + ": " + StepStatusText(verdict) + "\n\n";
		if (verdict != StepStatus::Pass) {
			notPassed += (notPassed.empty() ? "" : ", ") + report.Name();
		}
	}

	if (notPassed.empty()) {
		text += "Result: PASS (" + std::to_string(reports.size()) + " checks)\n";
	} else if (ExitCodeOf(reports) == ExitDevice) {
		text += "Result: ERROR, a check could not run: " + notPassed + "\n";
	} else {
		text += "Result: FAIL: " + notPassed + "\n";
	}

	return text;
}
