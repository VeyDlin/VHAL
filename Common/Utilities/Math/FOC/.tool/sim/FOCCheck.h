#pragma once
#include <cmath>
#include <cstdio>


// Shared helpers for the FOC host tests. Every test prints the worst error of each run against its bound, so the bounds
// can be read from the output, and a run over its bound adds one to the failure count.
namespace FOCCheck {

inline int failures = 0;


// The value of a float, double or IQ as a double, for the references and the error.
template<typename T>
double ToDouble(T value) {
	return static_cast<double>(value);
}


// The value of T nearest to x, read back as a double. A reference computed from these values sees the same inputs the
// class sees, so the error measures the arithmetic only.
template<typename T>
double Quantized(double x) {
	return static_cast<double>(T(x));
}


// Distance between two angles given in turns, measured around the circle (0 and 0.999 are 0.001 apart).
inline double CircularError(double a, double b) {
	double d = std::fabs(a - b);
	d = std::fmod(d, 1.0);
	return d > 0.5 ? 1.0 - d : d;
}


// Prints the worst error of a run and checks it against the bound. The name should say the class, the Q and the scenario.
inline void Report(const char* name, double maxError, double bound, int line) {
	bool passed = maxError <= bound;
	std::printf("%s: %s, max error %.3g, bound %.3g\n", name, passed ? "ok" : "FAIL", maxError, bound);
	if (!passed) {
		failures++;
		std::fprintf(stderr, "FOC check failed at line %d: %s\n", line, name);
	}
}


// A condition that must hold, for the checks that are not a numeric error.
inline void Expect(bool condition, const char* name, int line) {
	std::printf("%s: %s\n", name, condition ? "ok" : "FAIL");
	if (!condition) {
		failures++;
		std::fprintf(stderr, "FOC check failed at line %d: %s\n", line, name);
	}
}


// Exit code for main: 0 when every check passed.
inline int Finish() {
	if (failures != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}

}
