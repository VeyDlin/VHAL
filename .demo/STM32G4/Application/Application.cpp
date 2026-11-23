#include <Application.h>
// CMake defines VHAL_DEMO_<name> for the demo chosen with VHAL_DEMO. A demo folder needs its branch here, or the build stops.
#if defined(VHAL_DEMO_USBBench)
#include <DemoUSBBench/DemoUSBBench.h>
#elif defined(VHAL_DEMO_USBComposite)
#include <DemoUSBComposite/DemoUSBComposite.h>
#elif defined(VHAL_DEMO_USBStorage)
#include <DemoUSBStorage/DemoUSBStorage.h>
#elif defined(VHAL_DEMO_USBDfu)
#include <DemoUSBDfu/DemoUSBDfu.h>
#elif defined(VHAL_DEMO_USBNetwork)
#include <DemoUSBNetwork/DemoUSBNetwork.h>
#elif defined(VHAL_DEMO_USBAudio)
#include <DemoUSBAudio/DemoUSBAudio.h>
#elif defined(VHAL_DEMO_Regex)
#include <DemoRegex/DemoRegex.h>
#elif defined(VHAL_DEMO_Modbus)
#include <DemoModbus/DemoModbus.h>
#else
#error "No demo is selected: set VHAL_DEMO to a demo folder under Application/, and add its branch to Application/Application.cpp"
#endif


void Application::Run(ResultStatus consoleStatus) {
#if defined(VHAL_DEMO_USBBench)
	DemoUSBBench::Run(consoleStatus);
#elif defined(VHAL_DEMO_USBComposite)
	DemoUSBComposite::Run(consoleStatus);
#elif defined(VHAL_DEMO_USBStorage)
	DemoUSBStorage::Run(consoleStatus);
#elif defined(VHAL_DEMO_USBDfu)
	DemoUSBDfu::Run(consoleStatus);
#elif defined(VHAL_DEMO_USBNetwork)
	DemoUSBNetwork::Run(consoleStatus);
#elif defined(VHAL_DEMO_USBAudio)
	DemoUSBAudio::Run(consoleStatus);
#elif defined(VHAL_DEMO_Regex)
	DemoRegex::Run(consoleStatus);
#elif defined(VHAL_DEMO_Modbus)
	DemoModbus::Run(consoleStatus);
#endif
}
