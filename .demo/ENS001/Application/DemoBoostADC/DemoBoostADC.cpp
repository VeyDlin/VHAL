#include "DemoBoostADC.h"


namespace DemoBoostADC {

void Run() {
	// Configure ADC pin as analog input (GPIO21 = ADC channel 1)
	AGPIO::AnalogInit(AGPIO::AnalogParameters{
		.port = CMSDK_GPIO,
		.pin = Pin::Adc1
	});

	// Configure Boost: 11V output
	BSP::boost.SetParameters(BoostAdapter001::Parameters{
		.voltage = BoostAdapter001::VoltageSelect::V11
	});
	BSP::boost.Enable();

	// Wait for boost to stabilize
	System::DelayMs(50);

	// Configure ADC
	BSP::adc.SetParameters(AADC::Parameters{
		.resolution = AADC::Resolution::B12
	});
	BSP::adc.ConfigRegularGroup({}, { AADC::RegularChannel{ .channel = 1 } });

	// Read ADC in a loop
	uint16 adcValue = 0;
	while (true) {
		if (BSP::adc.ReadArray(&adcValue) == ResultStatus::ok) {
			// adcValue contains 12-bit ADC result (0..4095)
			// Use UART to print if available, or just toggle LED as heartbeat
		}
		System::DelayMs(100);
	}
}

}
