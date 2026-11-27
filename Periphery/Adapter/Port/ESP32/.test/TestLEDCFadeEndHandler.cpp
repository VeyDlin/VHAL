#include <VHAL.h>
#include <driver/ledc.h>
#include <Adapter/Port/ESP32/Common/LEDCAdapterESP.h>
#include <cstdio>

// Host tests for SetOnFadeEnd() on the ESP32 LEDC adapter, against the driver stub in shim/driver/ledc.h. The stub keeps the
// fade callback the driver would call. A clear must unregister it, because FadeCallback runs from the fade interrupt, and the
// adapter promises that an adapter without onFadeEnd never runs flash code from that interrupt. A channel gets its driver
// registration only once it has started a fade, so a handler set before that registers at the channel's first FadeTo().


namespace {


int failureCount = 0;


void ExpectTrue(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestLEDCFadeEndHandler.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define LEDC_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


// Stands in for the fade interrupt on one channel: calls the registered callback, if any, and reports whether one ran.
bool FireFadeEnd(ledc_channel_t channel) {
	ledc_cb_t callback = ledcStubRegistry.fadeCallback[LEDC_LOW_SPEED_MODE][channel];
	if (callback == nullptr) {
		return false;
	}

	ledc_cb_param_t parameter = {};
	parameter.event = LEDC_FADE_END_EVT;
	parameter.speed_mode = LEDC_LOW_SPEED_MODE;
	parameter.channel = channel;
	callback(&parameter, ledcStubRegistry.argument[LEDC_LOW_SPEED_MODE][channel]);
	return true;
}


void ResetDriver() {
	ledcStubRegistry = {};
}


// Makes an adapter with channel 0 on pin 2 and channel 1 on pin 3, both with a 13-bit duty resolution.
void SetUpAdapter(LEDCAdapterESP& led) {
	LEDCAdapter::Parameters params;
	params.dutyResolution = LEDCAdapterESP::Resolution::B13;
	led.SetParameters(params);

	LEDCAdapter::ChannelConfig config0;
	config0.channel = LEDCAdapterESP::Channel::C0;
	config0.pin = 2;
	led.AddChannel(config0);

	LEDCAdapter::ChannelConfig config1;
	config1.channel = LEDCAdapterESP::Channel::C1;
	config1.pin = 3;
	led.AddChannel(config1);
}


// Clearing the handler after a fade started stops the driver calling the fade-end callback.
void TestClearUnregistersFadeCallback() {
	ResetDriver();
	LEDCAdapterESP led(LEDCAdapterESP::Timer::T0, LEDCAdapterESP::SpeedMode::Low);
	SetUpAdapter(led);

	int handlerCalls = 0;
	led.SetOnFadeEnd([&](LEDCAdapter::ChannelOption) {
		handlerCalls++;
	});
	led.FadeTo(LEDCAdapterESP::Channel::C0, 100, 10);
	LEDC_TRUE(FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 1);

	led.SetOnFadeEnd(nullptr);
	LEDC_TRUE(!FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 1);
}


// A handler set after a fade started is registered at once, so it runs for that fade.
void TestHandlerSetAfterFadeIsRegistered() {
	ResetDriver();
	LEDCAdapterESP led(LEDCAdapterESP::Timer::T0, LEDCAdapterESP::SpeedMode::Low);
	SetUpAdapter(led);

	led.FadeTo(LEDCAdapterESP::Channel::C0, 100, 10);
	LEDC_TRUE(!FireFadeEnd(LEDC_CHANNEL_0));

	int handlerCalls = 0;
	led.SetOnFadeEnd([&](LEDCAdapter::ChannelOption) {
		handlerCalls++;
	});
	LEDC_TRUE(FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 1);
}


// A handler set after one channel faded registers only that channel. The other channel registers at its first FadeTo().
void TestHandlerSetRegistersOnlyFadedChannel() {
	ResetDriver();
	LEDCAdapterESP led(LEDCAdapterESP::Timer::T0, LEDCAdapterESP::SpeedMode::Low);
	SetUpAdapter(led);

	led.FadeTo(LEDCAdapterESP::Channel::C0, 100, 10);
	LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_0] == 0);

	int handlerCalls = 0;
	led.SetOnFadeEnd([&](LEDCAdapter::ChannelOption) {
		handlerCalls++;
	});
	LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_0] == 1);
	LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_1] == 0);
	LEDC_TRUE(!FireFadeEnd(LEDC_CHANNEL_1));

	LEDC_TRUE(FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 1);

	led.FadeTo(LEDCAdapterESP::Channel::C1, 100, 10);
	LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_1] == 1);
	LEDC_TRUE(FireFadeEnd(LEDC_CHANNEL_1));
	LEDC_TRUE(handlerCalls == 2);
}


// A handler set again after a clear is registered again.
void TestHandlerSetAgainAfterClearIsRegistered() {
	ResetDriver();
	LEDCAdapterESP led(LEDCAdapterESP::Timer::T0, LEDCAdapterESP::SpeedMode::Low);
	SetUpAdapter(led);

	int handlerCalls = 0;
	led.SetOnFadeEnd([&](LEDCAdapter::ChannelOption) {
		handlerCalls++;
	});
	led.FadeTo(LEDCAdapterESP::Channel::C0, 100, 10);
	led.SetOnFadeEnd(nullptr);
	LEDC_TRUE(!FireFadeEnd(LEDC_CHANNEL_0));

	led.SetOnFadeEnd([&](LEDCAdapter::ChannelOption) {
		handlerCalls++;
	});
	LEDC_TRUE(FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 1);
}


// SetParameters() may change the channel list, so the driver must hold no fade callback after it. A handler cleared
// afterwards stays cleared.
void TestSetParametersClearsDriverCallback() {
	ResetDriver();
	LEDCAdapterESP led(LEDCAdapterESP::Timer::T0, LEDCAdapterESP::SpeedMode::Low);
	SetUpAdapter(led);

	int handlerCalls = 0;
	led.SetOnFadeEnd([&](LEDCAdapter::ChannelOption) {
		handlerCalls++;
	});
	led.FadeTo(LEDCAdapterESP::Channel::C0, 100, 10);
	LEDC_TRUE(FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 1);

	LEDCAdapter::Parameters params;
	params.dutyResolution = LEDCAdapterESP::Resolution::B13;
	led.SetParameters(params);
	LEDC_TRUE(!FireFadeEnd(LEDC_CHANNEL_0));

	led.SetOnFadeEnd(nullptr);
	LEDC_TRUE(!FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 1);
}


// Destroying the adapter clears the driver's fade callback, so the driver never calls into a dead object.
void TestDestructorClearsDriverCallback() {
	ResetDriver();
	int handlerCalls = 0;
	{
		LEDCAdapterESP led(LEDCAdapterESP::Timer::T0, LEDCAdapterESP::SpeedMode::Low);
		SetUpAdapter(led);
		led.SetOnFadeEnd([&](LEDCAdapter::ChannelOption) {
			handlerCalls++;
		});
		led.FadeTo(LEDCAdapterESP::Channel::C0, 100, 10);
		LEDC_TRUE(ledcStubRegistry.fadeCallback[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_0] != nullptr);
	}
	LEDC_TRUE(!FireFadeEnd(LEDC_CHANNEL_0));
	LEDC_TRUE(handlerCalls == 0);
}


// A channel that never faded has no fade record in the driver, and the first register or fade call for a channel allocates
// it. Clearing the handler and destroying the adapter must not register channel 0, because it never faded. Channel 1 faded
// first, which also installs the fade service, so the handler change below runs its registration path.
void TestNeverFadedChannelGetsNoRegisterCall() {
	ResetDriver();
	{
		LEDCAdapterESP led(LEDCAdapterESP::Timer::T0, LEDCAdapterESP::SpeedMode::Low);
		SetUpAdapter(led);

		led.FadeTo(LEDCAdapterESP::Channel::C1, 100, 10);
		LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_0] == 0);

		led.SetOnFadeEnd([](LEDCAdapter::ChannelOption) { });
		LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_0] == 0);
		LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_1] == 1);

		led.SetOnFadeEnd(nullptr);
		LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_0] == 0);
	}
	LEDC_TRUE(ledcStubRegistry.registerCount[LEDC_LOW_SPEED_MODE][LEDC_CHANNEL_0] == 0);
}


}


int main() {
	TestClearUnregistersFadeCallback();
	TestHandlerSetAfterFadeIsRegistered();
	TestHandlerSetRegistersOnlyFadedChannel();
	TestHandlerSetAgainAfterClearIsRegistered();
	TestSetParametersClearsDriverCallback();
	TestDestructorClearsDriverCallback();
	TestNeverFadedChannelGetsNoRegisterCall();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestLEDCFadeEndHandler: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestLEDCFadeEndHandler: all checks passed\n");
	return 0;
}
