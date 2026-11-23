#include <VHAL.h>
#include <Stm32F4UartSim.h>
#include <Adapter/Port/STM32/F4/UARTAdapterF4.h>
#include <cstdio>


// Host tests for the F4 UART adapter's frame. With parity on and 8 data bits the word is 9 bits, so the word length
// bit M is set, and the received 9-bit frame gives its data byte with the parity bit left out. With 7 data bits and parity
// the word is 8 bits and the parity bit is masked off. Seven data bits without parity has no F4 word, so SetParameters
// refuses it before any register is written. The register model is in shim/Stm32F4UartSim.h.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestUartAdapterF4.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestUartAdapterF4.cpp:%d: %s is %llu, expected %llu\n", line, expression, actual, expected);
	}
}


#define EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define EXPECT_EQ(actual, expected) ExpectEqual(static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected), #actual, __LINE__)


USART_TypeDef regs;


// Even parity with the default 8 data bits: M is set and a 9-bit frame 0x1A5 gives the data byte 0xA5.
void TestEvenParityIsNineBitWord() {
	regs = USART_TypeDef{};
	UARTAdapterF4 uart(&regs);

	UARTAdapterF4::Parameters parameters;
	parameters.parity = UARTAdapterF4::Parity::Even;

	EXPECT(uart.SetParameters(parameters) == ResultStatus::ok);
	EXPECT((regs.CR1 & USART_CR1_M) != 0);

	SimUsartReceive(&regs, 0x1A5);
	uint8 received = 0;
	EXPECT(uart.ReadArray(&received, 1) == ResultStatus::ok);
	EXPECT_EQ(received, 0xA5);
}


// Even parity with 7 data bits: the word is 8 bits, M is clear and the parity bit 0x80 is masked off.
void TestSevenDataBitsWithParityIsEightBitWord() {
	regs = USART_TypeDef{};
	UARTAdapterF4 uart(&regs);

	UARTAdapterF4::Parameters parameters;
	parameters.parity = UARTAdapterF4::Parity::Even;
	parameters.dataBits = UARTAdapterF4::DataBits::B7;

	EXPECT(uart.SetParameters(parameters) == ResultStatus::ok);
	EXPECT((regs.CR1 & USART_CR1_M) == 0);

	SimUsartReceive(&regs, 0xA5);
	uint8 received = 0;
	EXPECT(uart.ReadArray(&received, 1) == ResultStatus::ok);
	EXPECT_EQ(received, 0x25);
}


// Seven data bits without parity has no F4 word: SetParameters returns notSupported and writes no register.
void TestSevenDataBitsWithoutParityIsRefused() {
	regs = USART_TypeDef{};
	UARTAdapterF4 uart(&regs);

	UARTAdapterF4::Parameters parameters;
	parameters.dataBits = UARTAdapterF4::DataBits::B7;

	EXPECT(uart.SetParameters(parameters) == ResultStatus::notSupported);
	EXPECT_EQ(regs.CR1, 0);
	EXPECT_EQ(regs.BRR, 0);
}


// A refused change on a running port leaves the stored frame as it was: 8E1 stays 8E1 and 0x1A5 still reads 0xA5.
void TestRefusedChangeKeepsStoredFrame() {
	regs = USART_TypeDef{};
	UARTAdapterF4 uart(&regs);

	UARTAdapterF4::Parameters running;
	running.parity = UARTAdapterF4::Parity::Even;
	EXPECT(uart.SetParameters(running) == ResultStatus::ok);

	UARTAdapterF4::Parameters refused;
	refused.dataBits = UARTAdapterF4::DataBits::B7;
	EXPECT(uart.SetParameters(refused) == ResultStatus::notSupported);

	EXPECT(uart.GetParameters().dataBits == UARTAdapterF4::DataBits::B8);
	EXPECT(uart.GetParameters().parity == UARTAdapterF4::Parity::Even);
	EXPECT((regs.CR1 & USART_CR1_M) != 0);

	SimUsartReceive(&regs, 0x1A5);
	uint8 received = 0;
	EXPECT(uart.ReadArray(&received, 1) == ResultStatus::ok);
	EXPECT_EQ(received, 0xA5);
}


// A change on a running port is applied: 8N1 at 115200 becomes 8E1 at 9600, so M is set and the port is enabled again.
void TestChangeOnRunningPortApplies() {
	regs = USART_TypeDef{};
	UARTAdapterF4 uart(&regs);

	UARTAdapterF4::Parameters first;
	EXPECT(uart.SetParameters(first) == ResultStatus::ok);
	EXPECT((regs.CR1 & USART_CR1_UE) != 0);
	EXPECT((regs.CR1 & USART_CR1_M) == 0);

	UARTAdapterF4::Parameters second;
	second.baudRate = 9600;
	second.parity = UARTAdapterF4::Parity::Even;
	EXPECT(uart.SetParameters(second) == ResultStatus::ok);
	EXPECT((regs.CR1 & USART_CR1_M) != 0);
	EXPECT((regs.CR1 & USART_CR1_UE) != 0);
	EXPECT_EQ(regs.BRR, 9600);
}


}


int main() {
	TestEvenParityIsNineBitWord();
	TestSevenDataBitsWithParityIsEightBitWord();
	TestSevenDataBitsWithoutParityIsRefused();
	TestRefusedChangeKeepsStoredFrame();
	TestChangeOnRunningPortApplies();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestUartAdapterF4: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestUartAdapterF4: all checks passed\n");
	return 0;
}
