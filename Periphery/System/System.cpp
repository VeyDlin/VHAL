#include "System.h"
#include "Console.h"
#include <string_view>


volatile uint32 System::tickCounter = 0;
float System::ticksInOneMs = 1;

std::function<void(const char *message, const char *file, uint32 line)> System::criticalErrorHandle = nullptr;
std::function<void(const char *string, size_t size)> System::writeHandle = nullptr;
std::function<int()> System::readHandle = nullptr;
std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;

Console& System::console = []() -> Console& {
    static Console instance; 
    return instance;
}();


void System::Init() {
    InitDelayDWT();
}


void System::TickHandler() {
    tickCounter++;
}


uint32_t System::GetTick() {
    return tickCounter;
}


uint32_t System::GetCoreTick() {
	#if defined(CoreDebug)
		return DWT->CYCCNT;
	#else
		return 0;
	#endif
}


uint32_t System::GetCoreClock() {
    return SystemCoreClock;
}


bool System::InitDelayDWT() {
	#if defined(CoreDebug)
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
		DWT->CYCCNT = 0;

		__asm volatile("NOP");
		__asm volatile("NOP");
		__asm volatile("NOP");

		return static_cast<bool>(DWT->CYCCNT);
	#else
		return false;
	#endif
}


void System::DelayMs(uint32_t delay) {
    if (rtosDelayMsHandle) {
        if(rtosDelayMsHandle(delay)) {
        	return;
        }
    }
    uint32_t endTick = GetTick() + delay * ticksInOneMs;
    while (GetTick() < endTick);
}


void System::DelayUs(uint32_t delay) {
#if defined(CoreDebug)
    const uint32_t startTick = GetCoreTick();
    const uint32_t ticks = delay * (GetCoreClock() / 1000000);

    while ((GetCoreTick() - startTick) < ticks);
#else
    DelayMs(delay / 1000);
#endif
}


void System::Reset() {
    NVIC_SystemReset();
}


System::DeviceId System::GetDeviceId() {
    return {LL_GetUID_Word0(), LL_GetUID_Word1(), LL_GetUID_Word2()};
}


void System::CriticalSection(bool isEnable) {
    if (isEnable) {
        __disable_irq();
    } else {
        __enable_irq();
    }
}


void System::Abort(const char* message, const char* file, uint32_t line) {
    CriticalSection(true);
    while (true);
}


void System::CriticalError(const char* message, const char* file, uint32_t line) {
    if (criticalErrorHandle) {
		console << Console::error << "System critical error" << Console::endl;
        if (line != 0) {
			console << "Line: " << line << Console::endl;
        }
        if (file != nullptr) {
			console << "File: " << file << Console::endl;
        }
        if (message != nullptr) {
			console << "Message: " << message << Console::endl;
        }

        criticalErrorHandle(const_cast<char*>(message), const_cast<char*>(file), line);
    }

    Abort();
}



// printf support
extern "C" {
	int _read(int file, char *ptr, int len) {
		if (System::readHandle != nullptr) {
			for (int i = 0; i < len; ++i) {
				int result = System::readHandle();
				if (result == EOF) {
					return i > 0 ? i : EOF;
				}
				ptr[i] = static_cast<char>(result);
			}
			return len;
		}
		return EOF;
	}

	int _write(int file, char *ptr, int len) {
		if(System::writeHandle != nullptr) {
			System::writeHandle(ptr, len);
		}
		return len;
	}
}



#ifdef USE_FULL_ASSERT
 	void assert_failed(uint8* file, uint32 line) {
 		System::CriticalError(nullptr, file, line);
 	}
#endif
