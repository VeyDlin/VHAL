#include <VHAL.h>
#include "GpioExtiSim.h"
#include <Adapter/Port/STM32/G4/GPIOAdapterG4.h>
#include "GpioExtiScenarios.h"


// Host tests for the G4 GPIO EXTI gate. The EXTI model and the scenarios are shared with the G0 and F4 tests.


int main() {
	return GpioExtiScenarios::RunGpioExtiScenarios<GPIOAdapterG4>("G4");
}
