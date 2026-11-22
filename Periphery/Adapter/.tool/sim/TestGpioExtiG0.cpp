#include <VHAL.h>
#include "GpioExtiSim.h"
#include <Adapter/Port/STM32/G0/GPIOAdapterG0.h>
#include "GpioExtiScenarios.h"


// Host tests for the G0 GPIO EXTI gate. The EXTI model and the scenarios are shared with the G4 and F4 tests.


int main() {
	return GpioExtiScenarios::RunGpioExtiScenarios<GPIOAdapterG0>("G0");
}
