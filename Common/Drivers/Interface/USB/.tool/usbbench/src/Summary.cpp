#include "Summary.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ExitCodes.h"

double MegabytesPerSecond(std::uint64_t bytes, double seconds) {
	double safeSeconds = seconds > 0.0 ? seconds : 1e-9;
	return static_cast<double>(bytes) / 1000000.0 / safeSeconds;
}


RunVerdict JudgeRun(bool transferOk, bool countersOk, bool dataOk) {
	if (!transferOk || !countersOk) {
		return RunVerdict::TransportFailure;
	}

	if (!dataOk) {
		return RunVerdict::DataFailure;
	}

	return RunVerdict::Pass;
}


RunVerdict WorstVerdict(const std::vector<DeviceResult>& results) {
	RunVerdict worst = RunVerdict::Pass;
	for (const DeviceResult& result : results) {
		if (static_cast<std::uint8_t>(result.verdict) > static_cast<std::uint8_t>(worst)) {
			worst = result.verdict;
		}
	}

	return worst;
}


int ExitCodeOf(RunVerdict verdict) {
	switch (verdict) {
		case RunVerdict::Pass:
			return ExitOk;
		case RunVerdict::DataFailure:
			return ExitData;
		case RunVerdict::TransportFailure:
			return ExitDevice;
	}

	return ExitDevice;
}


const char* VerdictText(RunVerdict verdict) {
	switch (verdict) {
		case RunVerdict::Pass:
			return "PASS";
		case RunVerdict::DataFailure:
			return "FAIL (data errors)";
		case RunVerdict::TransportFailure:
			return "FAIL (transport)";
	}

	return "FAIL (transport)";
}


SummaryTotals SumResults(const std::vector<DeviceResult>& results) {
	SummaryTotals totals;
	std::uint64_t inBytes = 0;
	std::uint64_t outBytes = 0;
	bool anyTimed = false;
	std::chrono::steady_clock::time_point earliest;
	std::chrono::steady_clock::time_point latest;
	for (const DeviceResult& result : results) {
		inBytes += result.inBytes;
		outBytes += result.outBytes;
		totals.mismatchBytes += result.mismatchBytes;
		totals.receiveErrors += result.receiveErrors;
		totals.devices++;
		if (result.verdict == RunVerdict::Pass) {
			totals.passed++;
		}

		if (!result.timed) {
			continue;
		}

		if (!anyTimed || result.started < earliest) {
			earliest = result.started;
		}

		if (!anyTimed || result.ended > latest) {
			latest = result.ended;
		}

		anyTimed = true;
	}

	if (anyTimed) {
		totals.windowSeconds = std::chrono::duration<double>(latest - earliest).count();
	}

	totals.inMegabytesPerSecond = MegabytesPerSecond(inBytes, totals.windowSeconds);
	totals.outMegabytesPerSecond = MegabytesPerSecond(outBytes, totals.windowSeconds);
	return totals;
}
