#include <Utilities/Regex/Regex.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string_view>


// Growth of the match time with the input length. The patterns (a*)*b and (a|aa)*c on runs of 'a' have no match, so each call scans the
// whole input. The times of 1000, 2000, 4000 and 8000 bytes are measured, five runs each, and the fastest run of each length is kept,
// because a loaded host only ever makes a run slower. Eight times the input takes eight times as long in linear time and 64 times as long
// in quadratic time, so the time at 8000 must be under 20 times the time at 1000. A ratio of neighbouring lengths is not checked: one
// disturbed run moves it past any useful bound. The times are printed.


namespace {


constexpr std::size_t Runs = 5;
constexpr std::size_t Lengths = 4;
// Calls per timed run. A single call takes about a millisecond at 8000 bytes, which a loaded host disturbs, so each run times a batch.
constexpr std::size_t Batch = 16;
constexpr std::array<std::size_t, Lengths> Sizes{1000, 2000, 4000, 8000};


std::array<char, 8000> run{};
volatile bool sink = false;
int failures = 0;


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
			std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
			for (std::size_t call = 0; call < Batch; call++) {
				if (useFind) {
					Result<typename Pattern::Captures> found = Pattern::Find(text);
					sink = found.IsOk();
				} else {
					sink = Pattern::Contains(text);
				}
			}

			std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
			times[repeat] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
		}

		fastest[length] = Fastest(times);
	}

	double ratio = static_cast<double>(fastest[3]) / static_cast<double>(fastest[0]);
	bool ok = ratio < 20.0;
	if (!ok) {
		failures++;
	}

	std::printf(
		"%s: fastest ns 1000=%lld 2000=%lld 4000=%lld 8000=%lld, ratio 8000/1000=%.2f %s\n",
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
