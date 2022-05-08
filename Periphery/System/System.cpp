#include "System.h"



volatile uint32 System::tickCounter = 0;
System::SystemPrint System::log;

std::function<void(char *string, uint32 line)> System::criticalErrorHandle;
std::function<void(char *string, size_t size)> System::writeHandle;
std::function<void(char *string, size_t size)> System::readHandle;
std::function<bool(uint32 delay)> System::rtosDelayMsHandle;




int _read(int file, char *ptr, int len) {
	if(System::writeHandle != nullptr) {
		System::readHandle(ptr, len);
	}
	return len;
}



int _write(int file, char *ptr, int len) {
	if(System::writeHandle != nullptr) {
		System::writeHandle(ptr, len);
	}
	return len;
}




#ifdef USE_FULL_ASSERT
 	void assert_failed(uint8* file, uint32 line) {
 		System::CriticalError(file, line);
 	}
#endif
