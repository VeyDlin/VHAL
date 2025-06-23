#include "System.h"
#include <string_view>
#ifdef USE_SYSTEM_CONSOLE
	#include <Utilities/Console/Console.h>
#endif


volatile uint64 System::tickCounter = 0;
float System::ticksInOneMs = 1;

std::function<void(const char *message, const char *file, uint32 line)> System::criticalErrorHandle = nullptr;
std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


#ifdef USE_SYSTEM_CONSOLE
	Console& System::console = []() -> Console& {
		static Console instance;
		return instance;
	}();
#endif


void System::SetWriteHandler(std::function<void(const char* string, size_t size)> handler) {
#ifdef USE_SYSTEM_CONSOLE
	console.SetWriteHandler(handler);
#endif
}


void System::SetReadHandler(std::function<int()> handler) {
#ifdef USE_SYSTEM_CONSOLE
	console.SetReadHandler(handler);
#endif
}


void System::Init() {
    InitDelayDWT();
}


void System::TickHandler() {
    tickCounter++;
}


uint64 System::GetTick() {
    return tickCounter;
}


uint64 System::GetMs() {
    return tickCounter * ticksInOneMs;
}


uint32 System::GetCoreTick() {
	#if defined(CoreDebug)
		return DWT->CYCCNT;
	#else
		return 0;
	#endif
}


uint32 System::GetCoreClock() {
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


void System::DelayMs(uint32 delay) {
    if (rtosDelayMsHandle) {
        if(rtosDelayMsHandle(delay)) {
        	return;
        }
    }
    uint32 endTick = GetTick() + delay * ticksInOneMs;
    while (GetTick() < endTick);
}


void System::DelayUs(uint32 delay) {
#if defined(CoreDebug)
    uint32 maxDelayTicks = 1 + (delay / 1000) + 1;
    uint32 startSysTick = GetTick();
    uint32 startCoreTick = GetCoreTick();
    uint32 targetDelta = delay * (GetCoreClock() / 1000000);

    while ((GetCoreTick() - startCoreTick) < targetDelta) {
    	if ((GetTick() - startSysTick) > maxDelayTicks) {
    		break;
    	}
    }
#else
    DelayMs(delay / 1000);
#endif
}


void System::Reset() {
    NVIC_SystemReset();
}


System::DeviceId System::GetDeviceId() {
	return { LL_GetUID_Word0(), LL_GetUID_Word1(), LL_GetUID_Word2() };
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
		#ifdef USE_SYSTEM_CONSOLE
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
		#endif

        criticalErrorHandle(const_cast<char*>(message), const_cast<char*>(file), line);
    }

    Abort();
}



// printf support
#ifdef USE_SYSTEM_CONSOLE
	extern "C" {
		int _read(int file, char *ptr, int len) {
			return System::console.Read(ptr, len);
		}

		int _write(int file, char *ptr, int len) {
			return System::console.Write(reinterpret_cast<const char*>(ptr), len);
		}
	}
#endif



#ifdef USE_FULL_ASSERT
 	void assert_failed(uint8* file, uint32 line) {
 		System::CriticalError(nullptr, file, line);
 	}
#endif
