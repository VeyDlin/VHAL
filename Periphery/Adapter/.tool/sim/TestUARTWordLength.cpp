#include <VHAL.h>
#include <Adapter/Port/STM32/Common/UARTWordLength.h>
#include <cstdint>
#include <cstdio>


// Host tests for the word length and receive mask that the STM32 UART ports choose from the data bits and the parity.
// The expected values are written from the reference manual rule for the USART word length: the parity bit is the top
// bit of the word, so 7 data bits with parity is an 8-bit word, and 8 data bits with parity is a 9-bit word. The F4 USART
// has no 7-bit word, so 7 data bits without parity is not supported there.


namespace {


int failureCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestUARTWordLength.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define WL_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


struct WordRow {
	const char* family;
	bool sevenBitWordAvailable;
	bool sevenDataBits;
	bool parityOn;
	UARTWordLength expectedWord;
	bool expectedSupported;
	uint8 expectedMask;
};


// Every combination of data bits and parity for the G0 and G4 families, and for the F4 family.
const WordRow rows[] = {
	{ "G0/G4", true,  true,  false, UARTWordLength::Bits7, true,  0x7F },
	{ "G0/G4", true,  true,  true,  UARTWordLength::Bits8, true,  0x7F },
	{ "G0/G4", true,  false, false, UARTWordLength::Bits8, true,  0xFF },
	{ "G0/G4", true,  false, true,  UARTWordLength::Bits9, true,  0xFF },
	{ "F4",    false, true,  false, UARTWordLength::Bits7, false, 0x7F },
	{ "F4",    false, true,  true,  UARTWordLength::Bits8, true,  0x7F },
	{ "F4",    false, false, false, UARTWordLength::Bits8, true,  0xFF },
	{ "F4",    false, false, true,  UARTWordLength::Bits9, true,  0xFF },
};


// Checks the word length, the support flag and the receive mask of one row. The mask is given by the data bits alone,
// so the parity does not change it.
void TestRow(const WordRow& row) {
	UARTWordChoice choice = ChooseUARTWordLength(row.sevenDataBits, row.parityOn, row.sevenBitWordAvailable);

	if (choice.wordLength != row.expectedWord) {
		failureCount++;
		std::fprintf(stderr, "TestUARTWordLength.cpp: %s data7=%d parity=%d: word length differs\n", row.family, row.sevenDataBits, row.parityOn);
	}

	if (choice.supported != row.expectedSupported) {
		failureCount++;
		std::fprintf(stderr, "TestUARTWordLength.cpp: %s data7=%d parity=%d: supported differs\n", row.family, row.sevenDataBits, row.parityOn);
	}

	if (UARTReceiveMask(row.sevenDataBits) != row.expectedMask) {
		failureCount++;
		std::fprintf(stderr, "TestUARTWordLength.cpp: %s data7=%d parity=%d: receive mask differs\n", row.family, row.sevenDataBits, row.parityOn);
	}
}


// Received word values with parity: the top bit of a 9-bit word is the parity bit and must not reach the byte.
void TestParityBitIsMasked() {
	// 8 data bits with parity: 0x1A5 is 0xA5 data plus a parity bit of 1.
	WL_EQ(static_cast<uint8>(0x1A5 & UARTReceiveMask(false)), 0xA5);

	// 7 data bits with parity: 0xA5 is 0x25 data plus a parity bit of 1 in the 8-bit word.
	WL_EQ(static_cast<uint8>(0xA5 & UARTReceiveMask(true)), 0x25);
}


}


int main() {
	for (const WordRow& row : rows) {
		TestRow(row);
	}

	TestParityBitIsMasked();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestUARTWordLength: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestUARTWordLength: all checks passed\n");
	return 0;
}
