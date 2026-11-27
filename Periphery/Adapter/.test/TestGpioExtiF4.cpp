#include <VHAL.h>
#include "GpioExtiSim.h"
#include <Adapter/Port/STM32/F4/GPIOAdapterF4.h>
#include "GpioExtiScenarios.h"


// Host tests for the F4 GPIO EXTI gate. The EXTI model and the scenarios are shared with the G0 and G4 tests.


int main() {
	return GpioExtiScenarios::RunGpioExtiScenarios<GPIOAdapterF4>("F4");
}
