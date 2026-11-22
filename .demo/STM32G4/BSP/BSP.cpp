#include <BSP.h>
// The CRS driver is not in the G4 platform include list (Platform.h), so the BSP, which owns the clocks, includes it.
#include <stm32g4xx_ll_crs.h>


AUART BSP::consoleSerial = { USART1 };
AUSB BSP::usbAdapter;
volatile uint32 BSP::consoleDropped = 0;
RingBuffer<uint8, BSP::ConsoleRingSize> BSP::consoleRing;
uint8 BSP::consoleChunk[BSP::ConsoleChunkSize] = {};


ResultStatus BSP::Init() {
	InitSystem();
	InitClock();
	InitSystemTick();
	System::Init();

	InitUsbClock();
	InitAdapterPeripheryEvents();
	InitIO();

	// The caller decides what a failed console means. The application keeps the status for SWD. Without a working USART1 no
	// write handler is set, so System::console writes nothing and never waits.
	ResultStatus status = consoleSerial.SetParameters({});
	if (status == ResultStatus::ok) {
		System::SetWriteHandler(ConsoleWrite);
		System::criticalErrorHandle = FlushConsoleOnError;
	}

	return status;
}


void BSP::ReadUniqueId(std::span<uint8, UniqueIdSize> uniqueId) {
	const volatile uint8* source = reinterpret_cast<const volatile uint8*>(UID_BASE);
	for (std::size_t index = 0; index < UniqueIdSize; index++) {
		uniqueId[index] = source[index];
	}
}


void BSP::InitSystem() {
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

	// The dead-battery pull-downs of the UCPD pins are not wanted: they would hold the pins that the board may use for
	// other signals. The bit lives in PWR_CR3 on the G4 family, which has no LL macro for it.
	PWR->CR3 |= PWR_CR3_UCPD_DBDIS;

	NVIC_SetPriority(SysTick_IRQn, 3);
}


void BSP::InitClock() {
	// Range 1 boost is what allows 170 MHz. It is set while the clock is still the 16 MHz reset clock, before the PLL starts.
	LL_PWR_EnableRange1BoostMode();

	// 170 MHz in range 1 boost needs four wait states. The latency is raised before the clock is switched, never after: a
	// faster clock with too few wait states reads stale or wrong flash words.
	LL_FLASH_SetLatency(LL_FLASH_LATENCY_4);
	while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_4) {}

	LL_FLASH_EnablePrefetch();
	LL_FLASH_EnableInstCache();
	LL_FLASH_EnableDataCache();

	LL_RCC_HSI_Enable();
	while (LL_RCC_HSI_IsReady() != 1) {}

	// HSI16 / 4 = 4 MHz at the PLL input, times 85 = 340 MHz VCO, divided by R = 2: 170 MHz SYSCLK.
	LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_4, 85, LL_RCC_PLLR_DIV_2);
	LL_RCC_PLL_Enable();
	LL_RCC_PLL_EnableDomain_SYS();
	while (LL_RCC_PLL_IsReady() != 1) {}

	LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
	LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
	LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
	while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {}

	LL_SetSystemCoreClock(170000000);
}


void BSP::InitSystemTick() {
	SysTick_Config(System::GetCoreClock() / 1000U);
}


void BSP::InitUsbClock() {
	// HSI48 is the 48 MHz USB clock. CRS trims it against the USB start-of-frame (1 kHz), so the clock needs no crystal and
	// stays within the USB tolerance. The reload value is 48 MHz / 1 kHz - 1.
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_CRS);

	LL_RCC_HSI48_Enable();
	while (LL_RCC_HSI48_IsReady() != 1) {}

	LL_CRS_ConfigSynchronization(
		LL_CRS_HSI48CALIBRATION_DEFAULT,
		LL_CRS_ERRORLIMIT_DEFAULT,
		47999U,
		LL_CRS_SYNC_SOURCE_USB | LL_CRS_SYNC_POLARITY_RISING | LL_CRS_SYNC_DIV_1
	);
	LL_CRS_EnableFreqErrorCounter();
	LL_CRS_EnableAutoTrimming();

	LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USB);

	// Only USB_LP serves the controller. USB_HP stays disabled in the NVIC: the adapter's flags would be served from two
	// vectors otherwise (see Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.h).
	NVIC_SetPriority(USB_LP_IRQn, 1);
	NVIC_EnableIRQ(USB_LP_IRQn);
}


void BSP::InitAdapterPeripheryEvents() {
	consoleSerial.beforePeripheryInit = []() -> ResultStatus {
		LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

		AGPIO::AlternateInit({ GPIOA, 9,  7, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // TX
		AGPIO::AlternateInit({ GPIOA, 10, 7, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // RX

		// The transmit interrupt sends the console text. It runs below the USB interrupt (priority 1), so USB is served first.
		NVIC_SetPriority(USART1_IRQn, 2);
		NVIC_EnableIRQ(USART1_IRQn);

		return ResultStatus::ok;
	};

	// Each transfer is one chunk, so the transmitter is idle again when it finishes. The next chunk is started from here while
	// the ring still has text; when the ring is empty, the next ConsoleWrite() starts the transfer.
	consoleSerial.SetOnInterrupt([](AUART::Irq irqType) {
		if (irqType == AUART::Irq::Tx) {
			StartConsoleChunk();
		}
	});
}


void BSP::InitIO() {
	// USB D- and D+ on PA11 and PA12, alternate function 10 on the G4 family. The USB transceiver drives them, so no pull.
	AGPIO::AlternateInit({ GPIOA, 11, 10, AGPIO::Pull::None, AGPIO::Speed::VeryHigh });
	AGPIO::AlternateInit({ GPIOA, 12, 10, AGPIO::Pull::None, AGPIO::Speed::VeryHigh });
}


// The write handler of System::console. It only stores the text and starts a transfer when the transmitter is idle, so the
// caller never waits for USART1. Characters that do not fit are counted in consoleDropped. The ring is written only here, from
// the main loop. It is read by this function and by the transmit-complete interrupt. Both start a chunk only while txState is
// ready, and the TX interrupts are enabled only while a chunk is in flight, so the two readers never run together. Enabling TX
// interrupts while idle, or starting a chunk without the ready check, breaks this.
void BSP::ConsoleWrite(const char* string, size_t size) {
	for (size_t index = 0; index < size; index++) {
		if (consoleRing.Push(static_cast<uint8>(string[index])) != ResultStatus::ok) {
			consoleDropped = consoleDropped + 1;
		}
	}

	if (consoleSerial.GetTxState() == ResultStatus::ready) {
		StartConsoleChunk();
	}
}


// Starts the next chunk of console text. It runs from ConsoleWrite() and from the transmit-complete interrupt. The bytes are
// taken from the ring only when the transmitter is ready, so a taken byte is always sent.
void BSP::StartConsoleChunk() {
	if (consoleSerial.GetTxState() != ResultStatus::ready) {
		return;
	}

	uint16 count = TakeConsoleChunk();
	if (count == 0) {
		return;
	}

	if (consoleSerial.WriteArrayAsync(consoleChunk, count) != ResultStatus::ok) {
		consoleDropped += count;
	}
}


// Moves the next chunk of console text from the ring into consoleChunk and returns its size. The caller owns the transmitter:
// StartConsoleChunk() checks that it is ready, and FlushConsoleOnError() has aborted the transfer first.
uint16 BSP::TakeConsoleChunk() {
	uint16 count = 0;
	while (count < ConsoleChunkSize && !consoleRing.IsEmpty()) {
		Result<uint8> data = consoleRing.Pop();
		if (!data.IsOk()) {
			break;
		}

		consoleChunk[count] = data.Value();
		count++;
	}

	return count;
}


// The criticalErrorHandle of System. CriticalError() has just written its lines into the ring, and Abort() masks the interrupts
// right after this returns. So the ring is written out here with blocking transfers. The transmit interrupt is stopped first, so
// no interrupt reads the ring during the drain. A blocking write gives up on its own timeout if USART1 does not answer.
void BSP::FlushConsoleOnError(const char*, const char*, uint32) {
	consoleSerial.AbortTransmit();

	while (!consoleRing.IsEmpty()) {
		uint16 count = TakeConsoleChunk();
		consoleSerial.WriteArray(consoleChunk, count);
	}
}
