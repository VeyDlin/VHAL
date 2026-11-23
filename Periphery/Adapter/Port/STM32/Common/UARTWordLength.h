#pragma once
#include <VHAL.h>


// Word length and receive mask of an STM32 USART frame, shared by the G0, G4 and F4 UART ports. The parity bit takes the
// top bit of the word, so with parity on the word is one bit longer than the data bits:
//
//   data bits   parity   word length   receive mask
//   7           off      7 bits        0x7F
//   7           on       8 bits        0x7F
//   8           off      8 bits        0xFF
//   8           on       9 bits        0xFF
//
// The mask follows the data bits, never the parity. A transmitted byte needs no mask: the write puts the byte in the
// data register and the hardware sets the parity bit itself.
//
// The F4 USART has no 7-bit word, so a 7-bit frame without parity is not possible there; the port passes
// sevenBitWordAvailable = false and gets supported = false back.
enum class UARTWordLength { Bits7, Bits8, Bits9 };


struct UARTWordChoice {
	UARTWordLength wordLength = UARTWordLength::Bits8;
	bool supported = true;
};


constexpr UARTWordChoice ChooseUARTWordLength(bool sevenDataBits, bool parityOn, bool sevenBitWordAvailable) {
	UARTWordChoice choice;

	if (!parityOn) {
		choice.wordLength = sevenDataBits ? UARTWordLength::Bits7 : UARTWordLength::Bits8;
	} else {
		choice.wordLength = sevenDataBits ? UARTWordLength::Bits8 : UARTWordLength::Bits9;
	}

	choice.supported = choice.wordLength != UARTWordLength::Bits7 || sevenBitWordAvailable;
	return choice;
}


constexpr uint8 UARTReceiveMask(bool sevenDataBits) {
	return sevenDataBits ? 0x7F : 0xFF;
}
