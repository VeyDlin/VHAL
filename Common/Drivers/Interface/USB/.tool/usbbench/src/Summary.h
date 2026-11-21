#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ExitCodes.h"

// How one run ended. A transport failure outranks a data error, and a data error outranks a pass, so the worst verdict
// of several devices is the one that sets the exit code.
enum class RunVerdict : std::uint8_t {
	Pass,
	DataFailure,
	TransportFailure
};

// The figures of one device's run that the --all summary shows. inBytes and outBytes are the bytes the checks verified.
// started and ended bound the device's transfers; they are only meaningful when timed is set, which is when the device
// ran its transfers at all.
struct DeviceResult {
	std::string serial;
	double inMegabytesPerSecond = 0.0;
	double outMegabytesPerSecond = 0.0;
	std::uint64_t inBytes = 0;
	std::uint64_t outBytes = 0;
	std::uint64_t mismatchBytes = 0;
	std::uint64_t receiveErrors = 0;
	bool timed = false;
	std::chrono::steady_clock::time_point started;
	std::chrono::steady_clock::time_point ended;
	RunVerdict verdict = RunVerdict::Pass;
};

// Sums over the devices of a parallel run. The rates are aggregate: the bytes of all devices in one direction divided by
// windowSeconds, the time from the earliest transfer start to the latest transfer end.
struct SummaryTotals {
	double inMegabytesPerSecond = 0.0;
	double outMegabytesPerSecond = 0.0;
	double windowSeconds = 0.0;
	std::uint64_t mismatchBytes = 0;
	std::uint64_t receiveErrors = 0;
	std::size_t devices = 0;
	std::size_t passed = 0;
};


// Megabytes per second (10^6 bytes). A zero time is treated as a tiny one, so the rate never divides by zero.
double MegabytesPerSecond(std::uint64_t bytes, double seconds);

// The verdict of one run from its checks. The transfers and the device counters must have worked before the data is
// judged, so a transport failure is reported first.
RunVerdict JudgeRun(bool transferOk, bool countersOk, bool dataOk);

// The worst verdict among the results. With no results it is a pass.
RunVerdict WorstVerdict(const std::vector<DeviceResult>& results);

// The process exit code for a verdict: pass 0, transport failure 1, data failure 3. Usage errors (2) are not verdicts.
int ExitCodeOf(RunVerdict verdict);

// The verdict as the report prints it.
const char* VerdictText(RunVerdict verdict);

// Adds up the bytes, mismatches and receive errors, counts the devices that passed, and computes the aggregate rates over
// the window of the timed devices. Untimed devices add nothing to the window or the bytes.
SummaryTotals SumResults(const std::vector<DeviceResult>& results);
