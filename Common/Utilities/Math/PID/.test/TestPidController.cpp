#include <Utilities/Math/PID/PidController.h>
#include <VhalCheck.h>
#include <cmath>


// Host tests of PidController<float>: a cyclic roll range that does not start at zero, the dead zone of a cyclic range across the
// wrap point, the integrator limit, and a sampling frequency of zero, which keeps the output and stays finite.


namespace {


// The roll range [10, 20] does not start at zero. Feedback 19 and reference 10 are one step apart across the wrap, so the
// proportional output is +1.
void CheckRollRangeNotStartingAtZero() {
	PidController<float>::Output output;
	output.max = 100.0f;
	output.min = -100.0f;

	PidController<float> pid({ 1.0f, 0.0f, 0.0f }, output);

	PidController<float>::Roll roll;
	roll.enable = true;
	roll.maxInput = 20.0f;
	roll.minInput = 10.0f;
	pid.SetRoll(roll);
	pid.SetReference(10.0f);
	pid.SetFeedback(19.0f);
	pid.Resolve();

	VHAL_CHECK_NEAR(pid.Get(), 1.0, 1e-4);
}


// One sample on the cyclic range [10, 370] (span 360) with a dead zone that does not go through the connection.
float RollDeadZoneError(float feedback, float reference, float deadZoneStart, float deadZoneEnd) {
	PidController<float>::Output output;
	output.max = 1000.0f;
	output.min = -1000.0f;
	PidController<float> pid({ 1.0f, 0.0f, 0.0f }, output);

	PidController<float>::Roll roll;
	roll.enable = true;
	roll.maxInput = 370.0f;
	roll.minInput = 10.0f;
	pid.SetRoll(roll);

	PidController<float>::RollDeadZone deadZone;
	deadZone.enable = true;
	deadZone.throughConnection = false;
	deadZone.start = deadZoneStart;
	deadZone.end = deadZoneEnd;
	pid.SetRollDeadZone(deadZone);

	pid.SetReference(reference);
	pid.SetFeedback(feedback);
	pid.Resolve();
	return pid.GetLastError();
}


// The path 200 -> 370 crosses [250, 260]; the error must go the other way round (-190), not through the zone.
void CheckRollDeadZoneUpperWrapRange() {
	float error = RollDeadZoneError(200.0f, 370.0f, 250.0f, 260.0f);
	VHAL_CHECK_NEAR(error, -190.0, 1e-3);
}


// The path 30 -> -30 crosses [340, 350] - 360 = [-20, -10] below the minimum; the error must be +300.
void CheckRollDeadZoneLowerWrapRange() {
	float error = RollDeadZoneError(30.0f, 330.0f, 340.0f, 350.0f);
	VHAL_CHECK_NEAR(error, 300.0, 1e-3);
}


// A small positive error crossing the zone must become -355; the sign, not the minimum of the range, picks the direction.
void CheckRollDeadZoneSmallPositiveError() {
	float error = RollDeadZoneError(200.0f, 205.0f, 202.0f, 204.0f);
	VHAL_CHECK_NEAR(error, -355.0, 1e-3);
}


void CheckIntegratorLimit() {
	PidController<float>::Output output;
	output.max = 100.0f;
	output.min = -100.0f;

	PidController<float> pid({ 0.0f, 1.0f, 0.0f }, output);
	PidController<float>::IntegratorLimit limit;
	limit.enable = true;
	limit.max = 0.5f;
	limit.min = -0.5f;
	pid.SetIntegratorLimit(limit);
	pid.SetReference(1.0f);
	pid.SetFeedback(0.0f);

	for (int step = 0; step < 1000; ++step) {
		pid.Resolve();
	}
	VHAL_CHECK_NEAR(pid.Get(), 0.5, 1e-4);
}


void CheckZeroFrequencyStaysFinite() {
	PidController<float>::Output output;
	output.max = 100.0f;
	output.min = -100.0f;

	PidController<float> pid({ 1.0f, 0.1f, 0.0f }, output);
	pid.SetFrequency(0);
	pid.SetReference(1.0f);
	pid.SetFeedback(0.0f);
	pid.Resolve();
	pid.SetFeedback(1.0f);
	pid.Resolve();

	VHAL_CHECK(std::isfinite(pid.Get()));
}


void CheckZeroFrequencyKeepsOutput() {
	PidController<float>::Output output;
	output.max = 100.0f;
	output.min = -100.0f;
	PidController<float> pid({ 1.0f, 0.0f, 0.0f }, output);
	pid.SetReference(5.0f);
	pid.SetFeedback(2.0f);
	pid.Resolve();
	float before = pid.Get();

	pid.SetFrequency(0);
	pid.SetFeedback(-10.0f);
	pid.Resolve();

	VHAL_CHECK(pid.Get() == before);
	VHAL_CHECK(pid.GetLastError() == 3.0f);
}


}


int main() {
	CheckRollRangeNotStartingAtZero();
	CheckRollDeadZoneUpperWrapRange();
	CheckRollDeadZoneLowerWrapRange();
	CheckRollDeadZoneSmallPositiveError();
	CheckIntegratorLimit();
	CheckZeroFrequencyStaysFinite();
	CheckZeroFrequencyKeepsOutput();
	return VhalCheck::Finish("TestPidController");
}
