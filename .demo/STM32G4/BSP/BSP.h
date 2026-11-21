#pragma once
#include <VHAL.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <cstddef>
#include <span>


// Board support for the STM32G431 benchmark board: 170 MHz SYSCLK from HSI16 through the PLL, a 1 kHz SysTick, USART1 on
// PA9/PA10 for the log (System::console), and the USB peripheral with its 48 MHz clock from HSI48 trimmed by CRS from the
// USB start-of-frame.
class BSP {
public:
	static AUART consoleSerial;
	static AUSB usbAdapter;
	// Characters System::console could not store because the ring was full. They are counted, not waited for. Readable over SWD.
	static volatile uint32 consoleDropped;
	// The MCU's 96-bit unique ID, in bytes. The USB demos turn it into their serial number.
	static constexpr std::size_t UniqueIdSize = 12;


private:
	// Console text waits in this ring until the USART1 transmit interrupt sends it. The interrupt sends it in chunks, and each
	// chunk is copied into a static buffer, because the transfer reads that buffer after the call returns.
	static constexpr uint16 ConsoleRingSize = 512;
	static constexpr uint16 ConsoleChunkSize = 32;
	static RingBuffer<uint8, ConsoleRingSize> consoleRing;
	static uint8 consoleChunk[ConsoleChunkSize];


public:
	// Brings up the clocks, the USB clock and the pins. Returns the result of USART1's setup, which the caller acts on.
	static ResultStatus Init();
	// Copies the MCU's unique ID into uniqueId. Every demo reads it here, so the register is read in one place.
	static void ReadUniqueId(std::span<uint8, UniqueIdSize> uniqueId);


private:
	static void InitSystem();
	static void InitClock();
	static void InitSystemTick();
	static void InitUsbClock();
	static void InitAdapterPeripheryEvents();
	static void InitIO();
	static void ConsoleWrite(const char* string, size_t size);
	static void StartConsoleChunk();
	static uint16 TakeConsoleChunk();
	static void FlushConsoleOnError(const char* message, const char* file, uint32 line);
};
