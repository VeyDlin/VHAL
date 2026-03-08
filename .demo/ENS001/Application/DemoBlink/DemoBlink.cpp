#include "DemoBlink.h"


namespace DemoBlink {

void Run() {
	AGPIO led(CMSDK_GPIO, Pin::Led);
	led.SetParameters({ AGPIO::Mode::Output });

	while (true) {
		led.Toggle();
		System::DelayMs(500);
	}
}

}
