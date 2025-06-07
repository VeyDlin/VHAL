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
	#define SystemAssert(condition, ...) if (!(condition)) { System::Abort(); }
	#define SystemAbort(...) System::Abort()
#endif

#ifdef USE_SYSTEM_CONSOLE
	class Console;
#endif
extern uint32 SystemCoreClock;


class System {
public:
#ifdef USE_SYSTEM_CONSOLE
	static Console &console;
#endif
	static std::function<void(const char *message, const char *file, uint32 line)> criticalErrorHandle;
	static std::function<bool(uint32 delay)> rtosDelayMsHandle;

	typedef union {
	    struct {
			uint8 waferX : 8;			// X coordinate on wafer
			uint8 waferY : 8;			// Y coordinate on wafer
			uint8 waferNumber : 8;		// Wafer number
			uint8 lotNumber : 8;		// Lot number (LSB)
			uint32 lotNumberCont : 24;	// Lot number continuation
			uint8 reserved : 8;			// [reserved]
			uint32 unique : 32;			// Unique ID
	    } _APacked fields;
	    uint32 words[3];
	    uint8 bytes[12];
	} DeviceId;


private:
	static volatile uint64 tickCounter;
	static float ticksInOneMs; // TODO: Convert Tick to ms


public:
    static void Init();
    static void TickHandler();
    static void SetWriteHandler(std::function<void(const char* string, size_t size)> handler);
    static void SetReadHandler(std::function<int()> handler);
    static uint64 GetTick();
    static uint64 GetMs();
    static uint32 GetCoreTick();
    static uint32 GetCoreClock();
    static void DelayMs(uint32 delay);
    static void DelayUs(uint32 delay);
    static void Reset();
	static DeviceId GetDeviceId();
    static void CriticalSection(bool isEnable);
    static void Abort(const char* message = nullptr, const char* file = nullptr, uint32 line = 0);
    static void CriticalError(const char* message = nullptr, const char* file = nullptr, uint32 line = 0);


private:
	static bool InitDelayDWT();
};
