#include <Application.h>


int main(void) {
	ResultStatus consoleStatus = BSP::Init();
	Application::Run(consoleStatus);
	return 0;
}
