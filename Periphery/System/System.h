#pragma once
#include <Periphery.h>
#include <Utilities/Status.h>
#include <Utilities/Print.h>
#include <limits>
#include <functional>



extern uint32 SystemCoreClock;


#define SystemAbort() System::CriticalError(__FILENAME_ONLY__, __LINE__)
#define SystemAssert(__e) ((__e) ? (void)0 : SystemAbort())




class System {
public:
	static std::function<void(char *string, uint32 line)> criticalErrorHandle;
	static std::function<void(char *string, size_t size)> writeHandle;
	static std::function<void(char *string, size_t size)> readHandle;
	static std::function<bool(uint32 delay)> rtosDelayMsHandle;

	union DeviceId {
		uint32 world[3];
		uint8 byte[12];
		uint16 halfWorld[6];
	};


private:
	static volatile uint32 tickCounter;
	static float ticksInOneMs; // TODO: Convert Tick to ms

	class SystemPrint: public Print {
	protected:
		virtual void _Write(char *string, size_t size) override {
			if(System::writeHandle != nullptr) {
				System::writeHandle(string, size);
			}
		}
	};



public:
	static SystemPrint log;



public:
	static void Init() {
		InitDelayDWT();
	}


	static inline void TickHandler() {
		tickCounter = tickCounter + 1;
	}


	static inline uint32 GetTick() {
		return tickCounter;
	}


	static inline uint32 GetCoreTick() {
		#if defined (CoreDebug)
			return DWT->CYCCNT;
		#else
			return 0;
		#endif
	}


	static inline uint32 GetCoreClock() {
		return SystemCoreClock;
	}


	static inline bool InitDelayDWT() {
		#if defined (CoreDebug)
			CoreDebug->DEMCR = CoreDebug->DEMCR & ~CoreDebug_DEMCR_TRCENA_Msk; 	// Disable TRC ~0x01000000;
			CoreDebug->DEMCR = CoreDebug->DEMCR | CoreDebug_DEMCR_TRCENA_Msk;  	// Enable TRC 0x01000000;
			DWT->CTRL = DWT->CTRL & ~DWT_CTRL_CYCCNTENA_Msk; 					// Disable clock cycle counter ~0x00000001;
			DWT->CTRL = DWT->CTRL | DWT_CTRL_CYCCNTENA_Msk; 					// Enable  clock cycle counter 0x00000001;
			DWT->CYCCNT = 0; 													// Reset the clock cycle counter value

			__asm volatile ("NOP");
			__asm volatile ("NOP");
			__asm volatile ("NOP");

			return static_cast<bool>(DWT->CYCCNT);
		#else
			return false;
		#endif
	}


	static void DelayMs(uint32 delay) {
		if(rtosDelayMsHandle != nullptr && rtosDelayMsHandle(delay)) {
			return;
		}
		uint32 ms = delay + (GetTick() * ticksInOneMs);
		while(GetTick() < ms);
	}


	static void DelayUs(uint32 delay) {
		#if defined (CoreDebug)
			volatile uint32 startUs = GetCoreTick();
			volatile uint32 startTick = GetTick();

			volatile uint32 endUs = delay;
			endUs *= GetCoreClock() / 1000000; // Core ticks in 1us
			endUs += startUs;

			volatile uint32 endTick = startTick + (ticksInOneMs * 2);

			if (endUs > startUs) {
				// Not overflowed
				while (GetCoreTick() < endUs && GetTick() < endTick);
			} else {
				// Overflowed
				while ((GetCoreTick() > startUs || GetCoreTick() < endUs) && GetTick() < endTick);
			}
		#else
			volatile waitIndex = ((delay * (SystemCoreClock / (100000 * 2))) / 10);
			while(wait != 0) {
				wait--;
			}
		#endif
	}


	static void Reset() {
		NVIC_SystemReset();
	}


	static DeviceId GetDeviceId() {
		return { LL_GetUID_Word0(), LL_GetUID_Word1(), LL_GetUID_Word2() };
	}


	static void CriticalSection(bool isEnable) {
		if(isEnable) {
			__disable_irq();
		} else {
			__enable_irq();
		}
	}


	static void CriticalError(const char* string = nullptr, uint32 line = 0) {
		if (criticalErrorHandle != nullptr) {
			log.WriteLn("System critical error");
			log.Write("Line: ");
			log.WriteLn(line);
			log.Write("Message: ");
			log.Write(string);

			criticalErrorHandle(const_cast<char*>(string), line);
		}

		CriticalSection(true);
		while (true);
	}
};



