#include <Utilities/Regex/Regex.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif


// Growth of the match time with the input length. The patterns (a*)*b and (a|aa)*c on runs of 'a' have no match, so each call scans the
// whole input. The CPU time of the calling thread for 1000, 2000, 4000 and 8000 bytes is measured, five runs each, and the fastest run of
// each length is kept, because a loaded host only ever makes a run slower. The thread's CPU time leaves out the time the host spends on
// other work, which wall-clock time would include. It is the cycle count on Windows (QueryThreadCycleTime) and the nanosecond count on
// POSIX (CLOCK_THREAD_CPUTIME_ID). Eight times the input takes eight times as long in linear time and 64 times as long in quadratic
// time, so the time at 8000 must be under 20 times the time at 1000. A ratio of neighbouring lengths is not checked: one disturbed run
// moves it past any useful bound. The times are printed.


namespace {


constexpr std::size_t Runs = 5;
constexpr std::size_t Lengths = 4;
// Calls per timed run. A single call takes about a millisecond at 8000 bytes, which a loaded host disturbs, so each run times a batch.
constexpr std::size_t Batch = 16;
constexpr std::array<std::size_t, Lengths> Sizes{1000, 2000, 4000, 8000};


std::array<char, 8000> run{};
volatile bool sink = false;
int failures = 0;


// The CPU time of the calling thread, in the unit described above. It returns false when the host cannot give it.
bool ThreadCpuTime(long long& value) {
#if defined(_WIN32)
	ULONG64 cycles = 0;
	if (!QueryThreadCycleTime(GetCurrentThread(), &cycles)) {
		return false;
	}

	value = static_cast<long long>(cycles);
	return true;
#else
	timespec spec{};
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &spec) != 0) {
		return false;
	}

	value = static_cast<long long>(spec.tv_sec) * 1000000000LL + static_cast<long long>(spec.tv_nsec);
	return true;
#endif
}


// The fastest of the runs.
long long Fastest(const std::array<long long, Runs>& values) {
	long long fastest = values[0];
	for (std::size_t index = 1; index < Runs; index++) {
		if (values[index] < fastest) {
			fastest = values[index];
		}
	}

	return fastest;
}


template<RegexBuilder (*Make)()>
void MeasureContains(const char* name, bool useFind) {
	using Pattern = Regex<Make>;
	std::array<long long, Lengths> fastest{};
	for (std::size_t length = 0; length < Lengths; length++) {
		std::array<long long, Runs> times{};
		std::string_view text(run.data(), Sizes[length]);
		for (std::size_t repeat = 0; repeat < Runs; repeat++) {
			long long begin = 0;
			ThreadCpuTime(begin);
			for (std::size_t call = 0; call < Batch; call++) {
				if (useFind) {
					Result<typename Pattern::Captures> found = Pattern::Find(text);
					sink = found.IsOk();
				} else {
					sink = Pattern::Contains(text);
				}
			}

			long long end = 0;
			ThreadCpuTime(end);
			times[repeat] = end - begin;
		}

		fastest[length] = Fastest(times);
	}

	double ratio = static_cast<double>(fastest[3]) / static_cast<double>(fastest[0]);
	bool ok = ratio < 20.0;
	if (!ok) {
		failures++;
	}

	std::printf(
		"%s: fastest CPU time 1000=%lld 2000=%lld 4000=%lld 8000=%lld, ratio 8000/1000=%.2f %s\n",
		name,
		fastest[0],
		fastest[1],
		fastest[2],
		fastest[3],
		ratio,
		ok ? "ok" : "FAILED"
	);
}


}


int main() {
	long long probe = 0;
	if (!ThreadCpuTime(probe)) {
		std::printf("linear time: the CPU time of the thread is not available\n");
		return 1;
	}

	for (std::size_t index = 0; index < run.size(); index++) {
		run[index] = 'a';
	}

	MeasureContains<+[] {
		return RegexBuilder("(a*)*b");
	}>("(a*)*b Contains", false);
	MeasureContains<+[] {
		return RegexBuilder("(a*)*b");
	}>("(a*)*b Find", true);
	MeasureContains<+[] {
		return RegexBuilder("(a|aa)*c");
	}>("(a|aa)*c Contains", false);
	MeasureContains<+[] {
		return RegexBuilder("(a|aa)*c");
	}>("(a|aa)*c Find", true);

	std::printf("linear time: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
