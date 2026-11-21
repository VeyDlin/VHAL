#include "CommandSim.h"
#include <Utilities/CommandLine/CommandRing.h>
#include <cstddef>


// The input ring of the console: bytes come out in the order they went in, a full ring refuses the byte and counts it as lost, and the
// count is taken once. The ring is tested one byte at a time, as the console feeds it.

int main() {
	CommandRing<4> ring;
	uint8 byte = 0;

	// An empty ring gives nothing, and nothing is lost.
	CommandExpect(!ring.Pop(byte));
	CommandExpect(ring.TakeLost() == 0);

	// Bytes come out in the order they went in, across the end of the buffer.
	for (uint8 round = 0; round < 3; round++) {
		for (uint8 value = 1; value <= 4; value++) {
			CommandExpect(ring.Push(static_cast<uint8>(round * 10 + value)));
		}

		for (uint8 value = 1; value <= 4; value++) {
			CommandExpect(ring.Pop(byte));
			CommandExpect(byte == round * 10 + value);
		}
	}

	CommandExpect(!ring.Pop(byte));

	// A full ring refuses the byte, keeps what it has, and counts each refused byte once.
	for (uint8 value = 0; value < 4; value++) {
		CommandExpect(ring.Push(value));
	}

	CommandExpect(!ring.Push(90));
	CommandExpect(!ring.Push(91));
	CommandExpect(ring.TakeLost() == 2);
	CommandExpect(ring.TakeLost() == 0);

	for (uint8 value = 0; value < 4; value++) {
		CommandExpect(ring.Pop(byte));
		CommandExpect(byte == value);
	}

	// The lost count is kept apart from the bytes: a byte pushed after a loss is stored.
	CommandExpect(ring.Push(7));
	CommandExpect(ring.Pop(byte));
	CommandExpect(byte == 7);
	CommandExpect(ring.TakeLost() == 0);

	// A ring of one byte: each byte is stored only after the previous one is taken.
	CommandRing<1> single;
	CommandExpect(single.Push(5));
	CommandExpect(!single.Push(6));
	CommandExpect(single.TakeLost() == 1);
	CommandExpect(single.Pop(byte));
	CommandExpect(byte == 5);
	CommandExpect(single.Push(8));
	CommandExpect(single.Pop(byte));
	CommandExpect(byte == 8);

	return CommandSim::Finish("TestCommandRing");
}
