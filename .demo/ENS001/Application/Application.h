#pragma once
#include <BSP.h>
// #include "DemoBlink/DemoBlink.h"
// #include "DemoUART/DemoUART.h"
// #include "DemoWaveGen/DemoWaveGen.h"
// #include "DemoBoostADC/DemoBoostADC.h"
#include "DemoStim/DemoStim.h"
#include "DemoStim/Modes/AnxietyModes.h"


class Application {
public:
	static void Init() {
		// Uncomment the desired demo:
		// DemoBlink::Run();
		// DemoUART::Run();
		// DemoWaveGen::Run();
		// DemoBoostADC::Run();
		DemoStim::Run(StimModes::Anxiety::HrvOptimal);

		while (true);
	}
};
