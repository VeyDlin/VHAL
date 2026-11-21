#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ExitCodes.h"
#include "Summary.h"

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


DeviceResult MakeResult(double inRate, double outRate, std::uint64_t mismatches, std::uint64_t errors, RunVerdict verdict) {
	DeviceResult result;
	result.inMegabytesPerSecond = inRate;
	result.outMegabytesPerSecond = outRate;
	result.mismatchBytes = mismatches;
	result.receiveErrors = errors;
	result.verdict = verdict;
	return result;
}


// A device that ran its transfers from startMs to endMs after the reference point, having verified the given bytes.
DeviceResult MakeTimed(std::uint64_t inBytes, std::uint64_t outBytes, long long startMs, long long endMs, RunVerdict verdict) {
	DeviceResult result = MakeResult(0.0, 0.0, 0, 0, verdict);
	result.inBytes = inBytes;
	result.outBytes = outBytes;
	result.timed = true;
	result.started = std::chrono::steady_clock::time_point(std::chrono::milliseconds(startMs));
	result.ended = std::chrono::steady_clock::time_point(std::chrono::milliseconds(endMs));
	return result;
}


void CheckJudge() {
	Expect(JudgeRun(true, true, true) == RunVerdict::Pass, "a clean run passes");
	Expect(JudgeRun(true, true, false) == RunVerdict::DataFailure, "bad data with a clean transfer is a data failure");
	Expect(JudgeRun(false, true, true) == RunVerdict::TransportFailure, "a failed transfer is a transport failure");
	Expect(JudgeRun(true, false, true) == RunVerdict::TransportFailure, "unreadable counters are a transport failure");
	Expect(JudgeRun(false, false, false) == RunVerdict::TransportFailure, "transport outranks data when both fail");
	Expect(JudgeRun(false, true, false) == RunVerdict::TransportFailure, "a transfer failure outranks bad data");
}


void CheckWorst() {
	std::vector<DeviceResult> none;
	Expect(WorstVerdict(none) == RunVerdict::Pass, "no results is a pass");

	std::vector<DeviceResult> allPass;
	allPass.push_back(MakeResult(1.0, 0.0, 0, 0, RunVerdict::Pass));
	allPass.push_back(MakeResult(1.0, 0.0, 0, 0, RunVerdict::Pass));
	Expect(WorstVerdict(allPass) == RunVerdict::Pass, "all passing is a pass");

	std::vector<DeviceResult> dataOnly;
	dataOnly.push_back(MakeResult(1.0, 0.0, 0, 0, RunVerdict::Pass));
	dataOnly.push_back(MakeResult(1.0, 0.0, 2, 0, RunVerdict::DataFailure));
	Expect(WorstVerdict(dataOnly) == RunVerdict::DataFailure, "one data failure makes the set a data failure");

	std::vector<DeviceResult> mixed;
	mixed.push_back(MakeResult(1.0, 0.0, 2, 0, RunVerdict::DataFailure));
	mixed.push_back(MakeResult(0.0, 0.0, 0, 0, RunVerdict::TransportFailure));
	mixed.push_back(MakeResult(1.0, 0.0, 0, 0, RunVerdict::Pass));
	Expect(WorstVerdict(mixed) == RunVerdict::TransportFailure, "a transport failure outranks a data failure");
}


// The exit code mapping of the README: 0 pass, 1 device or transport failure, 2 usage, 3 data error. A transport failure
// on any device outranks a data error on another.
void CheckExitCodes() {
	Expect(ExitOk == 0, "the pass exit code is 0");
	Expect(ExitDevice == 1, "the device or transport exit code is 1");
	Expect(ExitUsage == 2, "the usage exit code is 2");
	Expect(ExitData == 3, "the data error exit code is 3");

	Expect(ExitCodeOf(RunVerdict::Pass) == ExitOk, "a pass exits 0");
	Expect(ExitCodeOf(RunVerdict::DataFailure) == ExitData, "a data failure exits 3");
	Expect(ExitCodeOf(RunVerdict::TransportFailure) == ExitDevice, "a transport failure exits 1");

	std::vector<DeviceResult> dataAndTransport;
	dataAndTransport.push_back(MakeResult(1.0, 0.0, 2, 0, RunVerdict::DataFailure));
	dataAndTransport.push_back(MakeResult(0.0, 0.0, 0, 0, RunVerdict::TransportFailure));
	Expect(ExitCodeOf(WorstVerdict(dataAndTransport)) == ExitDevice, "a transport failure exits 1 even beside a data error");

	std::vector<DeviceResult> dataAndPass;
	dataAndPass.push_back(MakeResult(1.0, 0.0, 2, 0, RunVerdict::DataFailure));
	dataAndPass.push_back(MakeResult(1.0, 0.0, 0, 0, RunVerdict::Pass));
	Expect(ExitCodeOf(WorstVerdict(dataAndPass)) == ExitData, "a data error beside a pass exits 3");

	std::vector<DeviceResult> allPass;
	allPass.push_back(MakeResult(1.0, 0.0, 0, 0, RunVerdict::Pass));
	Expect(ExitCodeOf(WorstVerdict(allPass)) == ExitOk, "all passing exits 0");
}


void CheckVerdictText() {
	Expect(std::string(VerdictText(RunVerdict::Pass)) == "PASS", "a pass prints PASS");
	Expect(std::string(VerdictText(RunVerdict::DataFailure)) == "FAIL (data errors)", "a data failure prints its reason");
	Expect(std::string(VerdictText(RunVerdict::TransportFailure)) == "FAIL (transport)", "a transport failure prints its reason");
}


void CheckMegabytes() {
	Expect(std::fabs(MegabytesPerSecond(2000000, 2.0) - 1.0) < 1e-9, "2 MB in 2 s is 1 MB/s");
	Expect(MegabytesPerSecond(0, 0.0) == 0.0, "no bytes is a zero rate, even in no time");
}


// The total is the aggregate rate: all bytes over the window from the first start to the last end. The per-device rates
// are not added up, so two devices that overlap for a second are not counted twice.
void CheckAggregate() {
	std::vector<DeviceResult> results;
	results.push_back(MakeTimed(1000000, 500000, 0, 2000, RunVerdict::Pass));
	results.push_back(MakeTimed(1000000, 500000, 500, 1500, RunVerdict::Pass));

	SummaryTotals totals = SumResults(results);
	Expect(std::fabs(totals.windowSeconds - 2.0) < 1e-9, "the window runs from the earliest start to the latest end");
	Expect(std::fabs(totals.inMegabytesPerSecond - 1.0) < 1e-9, "the IN total is all bytes over the window");
	Expect(std::fabs(totals.outMegabytesPerSecond - 0.5) < 1e-9, "the OUT total is all bytes over the window");
	Expect(std::fabs(totals.inMegabytesPerSecond - 1.5) > 1e-6, "the IN total is not the sum of the per-device rates");
	Expect(totals.passed == 2 && totals.devices == 2, "both devices passed");
}


// A device that never ran its transfers adds no bytes and does not move the window.
void CheckAggregateIgnoresUntimed() {
	std::vector<DeviceResult> results;
	results.push_back(MakeTimed(1000000, 0, 0, 2000, RunVerdict::Pass));
	results.push_back(MakeResult(0.0, 0.0, 0, 0, RunVerdict::TransportFailure));
	results.push_back(MakeTimed(0, 0, -10000, 1000, RunVerdict::TransportFailure));
	results[2].timed = false;

	SummaryTotals totals = SumResults(results);
	Expect(std::fabs(totals.windowSeconds - 2.0) < 1e-9, "an untimed device does not move the window");
	Expect(std::fabs(totals.inMegabytesPerSecond - 0.5) < 1e-9, "an untimed device adds no bytes");
	Expect(totals.devices == 3 && totals.passed == 1, "untimed devices are still counted");
}


void CheckTotals() {
	std::vector<DeviceResult> results;
	DeviceResult failing = MakeTimed(1140000, 0, 0, 1000, RunVerdict::DataFailure);
	failing.mismatchBytes = 3;
	failing.receiveErrors = 1;
	results.push_back(MakeTimed(1152000, 0, 0, 1000, RunVerdict::Pass));
	results.push_back(failing);
	results.push_back(MakeResult(0.0, 0.0, 0, 0, RunVerdict::TransportFailure));

	SummaryTotals totals = SumResults(results);
	Expect(totals.devices == 3, "totals count every device");
	Expect(totals.passed == 1, "totals count only the devices that passed");
	Expect(std::fabs(totals.inMegabytesPerSecond - 2.292) < 1e-9, "totals add the IN bytes over the window");
	Expect(std::fabs(totals.outMegabytesPerSecond) < 1e-9, "totals add the OUT bytes over the window");
	Expect(totals.mismatchBytes == 3, "totals add the mismatches");
	Expect(totals.receiveErrors == 1, "totals add the receive errors");

	std::vector<DeviceResult> none;
	SummaryTotals empty = SumResults(none);
	Expect(empty.devices == 0 && empty.passed == 0, "no results gives empty totals");
	Expect(empty.inMegabytesPerSecond == 0.0 && empty.windowSeconds == 0.0, "no results gives a zero rate and window");

	std::vector<DeviceResult> bothPass;
	bothPass.push_back(MakeTimed(1000000, 1000000, 0, 1000, RunVerdict::Pass));
	bothPass.push_back(MakeTimed(1000000, 1000000, 0, 1000, RunVerdict::Pass));
	SummaryTotals passing = SumResults(bothPass);
	Expect(passing.devices == passing.passed, "every device passing makes the totals pass");
	Expect(std::fabs(passing.outMegabytesPerSecond - 2.0) < 1e-9, "totals add both directions over the window");
}


}


int main() {
	CheckJudge();
	CheckWorst();
	CheckExitCodes();
	CheckVerdictText();
	CheckMegabytes();
	CheckAggregate();
	CheckAggregateIgnoresUntimed();
	CheckTotals();

	std::printf("usbbench summary test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
