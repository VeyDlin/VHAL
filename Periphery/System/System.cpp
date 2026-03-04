#include <VHAL.h>
#include <string_view>
#ifdef VHAL_SYSTEM_CONSOLE
	#include <Utilities/Console/Console.h>
#endif


volatile uint64 System::tickCounter = 0;
float System::ticksInOneMs = 1;

std::function<void(const char *message, const char *file, uint32 line)> System::criticalErrorHandle = nullptr;
std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


#ifdef VHAL_SYSTEM_CONSOLE
	Console& System::console = []() -> Console& {
		static Console instance;
		return instance;
	}();
#endif


void System::SetWriteHandler(std::function<void(const char* string, size_t size)> handler) {
#ifdef VHAL_SYSTEM_CONSOLE
	console.SetWriteHandler(handler);
#endif
}


void System::SetReadHandler(std::function<int()> handler) {
#ifdef VHAL_SYSTEM_CONSOLE
	console.SetReadHandler(handler);
#endif
}


void System::Init() {
    InitDelayDWT();
}


void System::TickHandler() {
    tickCounter = tickCounter + 1;
}


uint64 System::GetTick() {
	__disable_irq();
	uint64 tick = tickCounter;
    __enable_irq();
    return tick;
}


uint64 System::GetMs() {
    return GetTick() * ticksInOneMs;
}


uint32 System::GetCoreClock() {
    return SystemCoreClock;
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


void System::Reset() {
    NVIC_SystemReset();
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
		#ifdef VHAL_SYSTEM_CONSOLE
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
#ifdef VHAL_SYSTEM_CONSOLE
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
