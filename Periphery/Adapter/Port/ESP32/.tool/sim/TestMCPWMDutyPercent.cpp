#include <VHAL.h>
#include <Adapter/Port/ESP32/Adapter/MCPWMAdapter.h>
#include <cstdio>

// Host tests for SetDutyPercent() on the MCPWM front end. A percent below 0 or above 100 is clamped to the range before it is
// scaled, so the compare value stays between 0 and the period. The test subclass records the value SetCompare() receives.


namespace {


int failureCount = 0;


void ExpectTrue(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestMCPWMDutyPercent.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define MCPWM_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


class RecordingMCPWM : public MCPWMAdapter {
public:
	uint32 lastCompare = 0xFFFFFFFF;


	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return ResultStatus::ok;
	}


	ResultStatus AddComparator(uint32) override {
		return ResultStatus::ok;
	}


	ResultStatus AddGenerator(GeneratorConfig, uint8) override {
		return ResultStatus::ok;
	}


	ResultStatus SetCompare(uint8, uint32 value) override {
		lastCompare = value;
		return ResultStatus::ok;
	}


	ResultStatus SetDeadTime(uint8, uint8, DeadTimeConfig) override {
		return ResultStatus::ok;
	}


	ResultStatus Start() override {
		return ResultStatus::ok;
	}


	ResultStatus Stop() override {
		return ResultStatus::ok;
	}
};


// A period of 20000 ticks: 100 percent is the full period.
void SetUpMCPWM(RecordingMCPWM& pwm) {
	MCPWMAdapter::Parameters params;
	params.periodTicks = 20000;
	pwm.SetParameters(params);
}


void TestPercentScalesToPeriod() {
	RecordingMCPWM pwm;
	SetUpMCPWM(pwm);

	pwm.SetDutyPercent(0, 25.0f);
	MCPWM_TRUE(pwm.lastCompare == 5000);

	pwm.SetDutyPercent(0, 100.0f);
	MCPWM_TRUE(pwm.lastCompare == 20000);

	pwm.SetDutyPercent(0, 0.0f);
	MCPWM_TRUE(pwm.lastCompare == 0);
}


void TestPercentAboveHundredIsClamped() {
	RecordingMCPWM pwm;
	SetUpMCPWM(pwm);

	pwm.SetDutyPercent(0, 150.0f);
	MCPWM_TRUE(pwm.lastCompare == 20000);

	pwm.SetDutyPercent(0, 1.0e6f);
	MCPWM_TRUE(pwm.lastCompare == 20000);
}


void TestNegativePercentIsClamped() {
	RecordingMCPWM pwm;
	SetUpMCPWM(pwm);

	pwm.SetDutyPercent(0, -10.0f);
	MCPWM_TRUE(pwm.lastCompare == 0);

	pwm.SetDutyPercent(0, -1.0e6f);
	MCPWM_TRUE(pwm.lastCompare == 0);
}


}


int main() {
	TestPercentScalesToPeriod();
	TestPercentAboveHundredIsClamped();
	TestNegativePercentIsClamped();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestMCPWMDutyPercent: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestMCPWMDutyPercent: all checks passed\n");
	return 0;
}
