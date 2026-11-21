#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "TransferLogic.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


// The 65-byte block of the device's answer: the 64-byte packet, a zero-length packet, then the last byte. Each read asks for the rest.
void CheckRemaining() {
	Expect(RemainingBytes(65, 0) == 65, "a read of nothing received asks for the whole block");
	Expect(RemainingBytes(65, 64) == 1, "after 64 bytes the next read asks for the last byte");
	Expect(RemainingBytes(65, 65) == 0, "a full block asks for nothing");
	Expect(RemainingBytes(65, 70) == 0, "more than the block asks for nothing and does not wrap");
}


void CheckAccumulation() {
	std::size_t received = 0;
	Expect(AddReadBytes(65, received, 64) && received == 64, "the 64-byte packet is taken");
	Expect(AddReadBytes(65, received, 0) && received == 64, "a zero-length packet is taken and changes nothing");
	Expect(AddReadBytes(65, received, 1) && received == 65, "the last byte completes the block");
}


void CheckOverflow() {
	std::size_t received = 64;
	Expect(!AddReadBytes(65, received, 2), "two bytes after 64 of 65 is more than the block has left");
	Expect(received == 64, "a refused read leaves the total unchanged");

	received = 0;
	Expect(AddReadBytes(64, received, 64) && received == 64, "a 64-byte block completed by a full packet");
	Expect(AddReadBytes(64, received, 0) && received == 64, "the zero-length packet after a full packet changes nothing");
	Expect(!AddReadBytes(64, received, 1), "a byte after the block is complete is refused");
}


void CheckDeadline() {
	Expect(MillisecondsUntil(1000, 400) == 600, "600 ms are left at 400 of 1000");
	Expect(MillisecondsUntil(1000, 1000) == 0, "no time is left at the deadline");
	Expect(MillisecondsUntil(1000, 1200) == 0, "no time is left after the deadline, with no wrap");
}


}


int main() {
	CheckRemaining();
	CheckAccumulation();
	CheckOverflow();
	CheckDeadline();

	std::printf("usbclasscheck transfer logic test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
