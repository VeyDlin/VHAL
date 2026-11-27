#include <VHAL.h>
#include <Adapter/Port/ESP32/Adapter/LEDCAdapter.h>
#include <cstdio>

// Host tests for SetDutyPercent() on the LEDC front end. A percent below 0 or above 100 is clamped to the range before it is
// scaled, so the duty stays between 0 and the maximum of the resolution. The test subclass records the duty SetDuty() receives.


namespace {


int failureCount = 0;


void ExpectTrue(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestLEDCDutyPercent.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define LEDC_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


class RecordingLEDC : public LEDCAdapter {
public:
	uint32 lastDuty = 0xFFFFFFFF;


	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return ResultStatus::ok;
	}


	ResultStatus AddChannel(ChannelConfig) override {
		return ResultStatus::ok;
	}


	ResultStatus SetDuty(ChannelOption, uint32 duty) override {
		lastDuty = duty;
		return ResultStatus::ok;
	}


	ResultStatus SetFrequency(uint32) override {
		return ResultStatus::ok;
	}


	uint32 GetFrequency() override {
		return 0;
	}


	uint32 GetDuty(ChannelOption) override {
		return lastDuty;
	}


	ResultStatus FadeTo(ChannelOption, uint32, uint32) override {
		return ResultStatus::ok;
	}


	ResultStatus Stop(ChannelOption, uint32) override {
		return ResultStatus::ok;
	}
};


// A 13-bit resolution gives a maximum duty of 8192.
void SetUpLEDC(RecordingLEDC& led) {
	LEDCAdapter::Parameters params;
	params.dutyResolution = LEDCAdapter::ResolutionOption(13);
	led.SetParameters(params);
}


void TestPercentScalesToResolution() {
	RecordingLEDC led;
	SetUpLEDC(led);
	LEDCAdapter::ChannelOption channel(0);

	led.SetDutyPercent(channel, 50.0f);
	LEDC_TRUE(led.lastDuty == 4096);

	led.SetDutyPercent(channel, 100.0f);
	LEDC_TRUE(led.lastDuty == 8192);

	led.SetDutyPercent(channel, 0.0f);
	LEDC_TRUE(led.lastDuty == 0);
}


void TestPercentAboveHundredIsClamped() {
	RecordingLEDC led;
	SetUpLEDC(led);
	LEDCAdapter::ChannelOption channel(0);

	led.SetDutyPercent(channel, 150.0f);
	LEDC_TRUE(led.lastDuty == 8192);

	led.SetDutyPercent(channel, 1.0e6f);
	LEDC_TRUE(led.lastDuty == 8192);
}


void TestNegativePercentIsClamped() {
	RecordingLEDC led;
	SetUpLEDC(led);
	LEDCAdapter::ChannelOption channel(0);

	led.SetDutyPercent(channel, -10.0f);
	LEDC_TRUE(led.lastDuty == 0);

	led.SetDutyPercent(channel, -1.0e6f);
	LEDC_TRUE(led.lastDuty == 0);
}


}


int main() {
	TestPercentScalesToResolution();
	TestPercentAboveHundredIsClamped();
	TestNegativePercentIsClamped();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestLEDCDutyPercent: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestLEDCDutyPercent: all checks passed\n");
	return 0;
}
