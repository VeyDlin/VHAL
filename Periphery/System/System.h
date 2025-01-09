#pragma once
#include <Periphery.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Status.h>
#include <functional>
#include <limits>
#include <string_view>


#define ___SystemGetMacro(_1, _2, NAME, ...) NAME

#define ___SystemAssert1(condition) System::CriticalError(#condition, __FILENAME_ONLY__, __LINE__)
#define ___SystemAssert2(condition, message) System::CriticalError(message, __FILENAME_ONLY__, __LINE__)

#define ___SystemAbort1() System::CriticalError(nullptr, __FILENAME_ONLY__, __LINE__)
#define ___SystemAbort2(message) System::CriticalError(message, __FILENAME_ONLY__, __LINE__)

#ifdef USE_FULL_ASSERT
	#define SystemAssert(...) ___SystemGetMacro(__VA_ARGS__, ___SystemAssert2, ___SystemAssert1)(__VA_ARGS__)
	#define SystemAbort(...) ___SystemGetMacro(__VA_ARGS__, ___SystemAbort2, ___SystemAbort1)(__VA_ARGS__)
#else
	#define SystemAssert(...) System::Abort()
	#define SystemAbort(...) System::Abort()
#endif


extern uint32 SystemCoreClock;
class Console;


class System {
public:
	static Console &console;
	static std::function<void(const char *message, const char *file, uint32 line)> criticalErrorHandle;
	static std::function<void(const char *string, size_t size)> writeHandle;
	static std::function<int()> readHandle;
	static std::function<bool(uint32 delay)> rtosDelayMsHandle;

	union DeviceId {
		uint32 world[3];
		uint8 byte[12];
		uint16 halfWorld[6];
	};


private:
	static volatile uint32 tickCounter;
	static float ticksInOneMs; // TODO: Convert Tick to ms


public:
    static void Init();
    static void TickHandler();
    static uint32_t GetTick();
    static uint32_t GetCoreTick();
    static uint32_t GetCoreClock();
    static void DelayMs(uint32_t delay);
    static void DelayUs(uint32_t delay);
    static void Reset();
	static DeviceId GetDeviceId();
    static void CriticalSection(bool isEnable);
    static void Abort(const char* message = nullptr, const char* file = nullptr, uint32_t line = 0);
    static void CriticalError(const char* message = nullptr, const char* file = nullptr, uint32_t line = 0);


private:
	static bool InitDelayDWT();
};