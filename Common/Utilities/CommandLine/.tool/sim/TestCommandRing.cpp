#include "CommandSim.h"
#include <Utilities/CommandLine/CommandRing.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <vector>


// The input ring of the console: bytes come out in the order they went in, a full ring refuses the byte and counts it as lost, and the
// count is taken once. The ring is tested one byte at a time, as the console feeds it.
//
// Each single-thread case runs twice: once from position 0, and once from 2^32 - 3, so that the positions cross the wrap of a 32-bit
// counter. The expected results are the same in both runs.

constexpr uint32 nearWrap = 0xFFFFFFFDu; // 2^32 - 3


// The refused Ctrl+C presses are tested with the consumer's calls in the order the console makes them: InterruptDue before each Pop.
void TestRefusedInterrupts(uint32 start) {
	uint8 byte = 0;

	// One refused Ctrl+C fires once, at the place where it is refused: the bytes accepted before it are read first.
	CommandRing<4> oneRefusal{start};
	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(oneRefusal.Push(value));
	}

	CommandExpect(!oneRefusal.Push(0x03));
	CommandExpect(oneRefusal.RefuseInterrupt());
	CommandExpect(oneRefusal.InterruptWaiting());

	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(!oneRefusal.InterruptDue());
		CommandExpect(oneRefusal.Pop(byte) && byte == value);
	}

	CommandExpect(oneRefusal.InterruptDue());
	CommandExpect(!oneRefusal.InterruptDue());
	CommandExpect(!oneRefusal.InterruptWaiting());
	CommandExpect(!oneRefusal.Pop(byte));

	// Two refused Ctrl+C presses at the same place each fire once, one after the other, when the consumer reaches that place.
	CommandRing<4> twin{start};
	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(twin.Push(value));
	}

	CommandExpect(!twin.Push(0x03));
	CommandExpect(twin.RefuseInterrupt());
	CommandExpect(twin.RefuseInterrupt());

	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(!twin.InterruptDue());
		CommandExpect(twin.Pop(byte) && byte == value);
	}

	CommandExpect(twin.InterruptDue());
	CommandExpect(twin.InterruptDue());
	CommandExpect(!twin.InterruptDue());
	CommandExpect(!twin.InterruptWaiting());
	CommandExpect(!twin.Pop(byte));

	// Two refused presses with bytes read between them have two places. The first fires at 4, the second at 5, and each fires once.
	CommandRing<4> apart{start};
	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(apart.Push(value));
	}

	CommandExpect(!apart.Push(0x03));
	CommandExpect(apart.RefuseInterrupt());
	CommandExpect(apart.Pop(byte) && byte == 1);
	CommandExpect(apart.Push(5));
	CommandExpect(!apart.Push(0x03));
	CommandExpect(apart.RefuseInterrupt());

	CommandExpect(!apart.InterruptDue());
	CommandExpect(apart.Pop(byte) && byte == 2);
	CommandExpect(!apart.InterruptDue());
	CommandExpect(apart.Pop(byte) && byte == 3);
	CommandExpect(!apart.InterruptDue());
	CommandExpect(apart.Pop(byte) && byte == 4);
	CommandExpect(apart.InterruptDue());
	CommandExpect(!apart.InterruptDue());
	CommandExpect(apart.Pop(byte) && byte == 5);
	CommandExpect(apart.InterruptDue());
	CommandExpect(!apart.InterruptDue());
	CommandExpect(!apart.InterruptWaiting());
	CommandExpect(!apart.Pop(byte));

	// A third refused press while two wait is dropped: RefuseInterrupt returns false and leaves no place. Only the two that wait fire.
	CommandRing<4> crowd{start};
	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(crowd.Push(value));
	}

	CommandExpect(!crowd.Push(0x03));
	CommandExpect(crowd.RefuseInterrupt());
	CommandExpect(crowd.RefuseInterrupt());
	CommandExpect(!crowd.RefuseInterrupt());

	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(!crowd.InterruptDue());
		CommandExpect(crowd.Pop(byte) && byte == value);
	}

	CommandExpect(crowd.InterruptDue());
	CommandExpect(crowd.InterruptDue());
	CommandExpect(!crowd.InterruptDue());
	CommandExpect(!crowd.InterruptWaiting());

	// Both taken, so a new refusal takes a free slot and fires at its own place, 8.
	for (uint8 value = 6; value <= 9; value++) {
		CommandExpect(crowd.Push(value));
	}

	CommandExpect(!crowd.Push(0x03));
	CommandExpect(crowd.RefuseInterrupt());

	for (uint8 value = 6; value <= 9; value++) {
		CommandExpect(!crowd.InterruptDue());
		CommandExpect(crowd.Pop(byte) && byte == value);
	}

	CommandExpect(crowd.InterruptDue());
	CommandExpect(!crowd.InterruptDue());
	CommandExpect(!crowd.InterruptWaiting());

	// TakeWaitingInterrupt takes only the oldest press, and drops only the bytes before its place. The bytes after it stay, and the newer
	// press keeps its place at 5 and fires there.
	CommandRing<4> taken{start};
	for (uint8 value = 1; value <= 4; value++) {
		CommandExpect(taken.Push(value));
	}

	CommandExpect(!taken.Push(0x03));
	CommandExpect(taken.RefuseInterrupt());
	CommandExpect(taken.Pop(byte) && byte == 1);
	CommandExpect(taken.Push(5));
	CommandExpect(!taken.Push(0x03));
	CommandExpect(taken.RefuseInterrupt());

	CommandExpect(taken.InterruptWaiting());
	taken.TakeWaitingInterrupt();
	CommandExpect(taken.InterruptWaiting());
	CommandExpect(!taken.InterruptDue());
	CommandExpect(taken.Pop(byte) && byte == 5);
	CommandExpect(taken.InterruptDue());
	CommandExpect(!taken.InterruptDue());
	CommandExpect(!taken.InterruptWaiting());
	CommandExpect(!taken.Pop(byte));

	// DiscardTo drops up to an accepted Ctrl+C that lies before a waiting press, and the press is not taken: it fires at its own place. The
	// ring holds a, b, c and an accepted Ctrl+C, and the refused press lies after them, at 4.
	CommandRing<4> cancelled{start};
	CommandExpect(cancelled.Push('a'));
	CommandExpect(cancelled.Push('b'));
	CommandExpect(cancelled.Push('c'));
	CommandExpect(cancelled.Push(0x03));
	CommandExpect(!cancelled.Push(0x03));
	CommandExpect(cancelled.RefuseInterrupt());

	CommandExpect(cancelled.DiscardTo(0x03));
	CommandExpect(cancelled.InterruptWaiting());
	CommandExpect(cancelled.InterruptDue());
	CommandExpect(!cancelled.InterruptDue());
	CommandExpect(!cancelled.InterruptWaiting());
	CommandExpect(!cancelled.Pop(byte));

	// An accepted Ctrl+C before a waiting press is the cancel's, and the bytes after it stay: the press keeps its place at 4, and the
	// bytes b and c are read before the press fires.
	CommandRing<4> before{start};
	CommandExpect(before.Push('a'));
	CommandExpect(before.Push(0x03));
	CommandExpect(before.Push('b'));
	CommandExpect(before.Push('c'));
	CommandExpect(!before.Push('d'));
	CommandExpect(before.RefuseInterrupt());

	CommandExpect(before.DiscardTo(0x03));
	CommandExpect(before.InterruptWaiting());
	CommandExpect(!before.InterruptDue());
	CommandExpect(before.Pop(byte) && byte == 'b');
	CommandExpect(before.Pop(byte) && byte == 'c');
	CommandExpect(before.InterruptDue());
	CommandExpect(!before.Pop(byte));
	CommandExpect(!before.InterruptWaiting());

	// DiscardTo never looks past a waiting press: an accepted Ctrl+C after it is not the cancel's, so it returns false and drops nothing.
	// The press fires at 2, and the Ctrl+C at 2 is read after it.
	CommandRing<4> after{start};
	CommandExpect(after.Push('a'));
	CommandExpect(after.Push('b'));
	CommandExpect(after.RefuseInterrupt());
	CommandExpect(after.Push(0x03));
	CommandExpect(after.Push('c'));

	CommandExpect(!after.DiscardTo(0x03));
	CommandExpect(after.InterruptWaiting());
	CommandExpect(after.Pop(byte) && byte == 'a');
	CommandExpect(!after.InterruptDue());
	CommandExpect(after.Pop(byte) && byte == 'b');
	CommandExpect(after.InterruptDue());
	CommandExpect(after.Pop(byte) && byte == 0x03);
	CommandExpect(after.Pop(byte) && byte == 'c');
	CommandExpect(!after.InterruptWaiting());
	CommandExpect(!after.Pop(byte));

	// A byte at the place of a refused Ctrl+C is not popped before that press fires. The consumer checked for a press before the producer
	// refused it, so the Pop is the only call that can see the press. Pop returns false at place 1, and InterruptDue then fires before b.
	CommandRing<4> race{start};
	CommandExpect(race.Push('a'));
	CommandExpect(!race.InterruptDue());
	CommandExpect(race.RefuseInterrupt());
	CommandExpect(race.Push('b'));
	CommandExpect(race.Pop(byte) && byte == 'a');
	CommandExpect(!race.Pop(byte));
	CommandExpect(race.InterruptDue());
	CommandExpect(race.Pop(byte) && byte == 'b');
	CommandExpect(!race.InterruptDue());
	CommandExpect(!race.Pop(byte));
}


// The single-thread cases, from the given start position.
void TestSingleThread(uint32 start) {
	CommandRing<4> ring{start};
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
	CommandRing<1> single{start};
	CommandExpect(single.Push(5));
	CommandExpect(!single.Push(6));
	CommandExpect(single.TakeLost() == 1);
	CommandExpect(single.Pop(byte));
	CommandExpect(byte == 5);
	CommandExpect(single.Push(8));
	CommandExpect(single.Pop(byte));
	CommandExpect(byte == 8);

	TestRefusedInterrupts(start);
}


// The reviewer's case. A ring of 4 bytes starts at 2^32 - 2, so the four bytes cross the wrap. The refused Ctrl+C has its place after d.
// The Interrupt must come after d, and not before a, b or c.
void TestRefusedInterruptAcrossWrap() {
	constexpr uint32 start = 0xFFFFFFFEu; // 2^32 - 2
	CommandRing<4> ring{start};
	uint8 byte = 0;

	for (uint8 value = 'a'; value <= 'd'; value++) {
		CommandExpect(ring.Push(value));
	}

	CommandExpect(!ring.Push(0x03));
	CommandExpect(ring.RefuseInterrupt());

	for (uint8 value = 'a'; value <= 'd'; value++) {
		CommandExpect(!ring.InterruptDue());
		CommandExpect(ring.Pop(byte) && byte == value);
	}

	CommandExpect(ring.InterruptDue());
	CommandExpect(!ring.InterruptDue());
	CommandExpect(!ring.InterruptWaiting());
	CommandExpect(!ring.Pop(byte));
}


// TakeWaitingInterrupt across the wrap. The ring starts at 2^32 - 2. The oldest press waits at position 2 after the wrap (the fifth byte
// from the start), so it drops b, c and d, and the byte e, pushed after it, stays. The newer press waits at position 3, and it keeps it.
void TestTakeWaitingAcrossWrap() {
	constexpr uint32 start = 0xFFFFFFFEu; // 2^32 - 2
	CommandRing<4> ring{start};
	uint8 byte = 0;

	for (uint8 value = 'a'; value <= 'd'; value++) {
		CommandExpect(ring.Push(value));
	}

	CommandExpect(!ring.Push(0x03));
	CommandExpect(ring.RefuseInterrupt());
	CommandExpect(ring.Pop(byte) && byte == 'a');
	CommandExpect(ring.Push('e'));
	CommandExpect(!ring.Push(0x03));
	CommandExpect(ring.RefuseInterrupt());

	CommandExpect(ring.InterruptWaiting());
	ring.TakeWaitingInterrupt();
	CommandExpect(ring.InterruptWaiting());
	CommandExpect(!ring.InterruptDue());
	CommandExpect(ring.Pop(byte) && byte == 'e');
	CommandExpect(ring.InterruptDue());
	CommandExpect(!ring.InterruptDue());
	CommandExpect(!ring.InterruptWaiting());
	CommandExpect(!ring.Pop(byte));
}


// DiscardTo across the wrap. The ring starts at 2^32 - 2. The first case cancels at an accepted Ctrl+C that lies before the place. The
// second case has a press at position 0 after the wrap (the third byte from the start), with written already past it, so the search must
// stop at that place and find nothing.
void TestDiscardAcrossWrap() {
	constexpr uint32 start = 0xFFFFFFFEu; // 2^32 - 2
	uint8 byte = 0;

	CommandRing<4> cancelled{start};
	CommandExpect(cancelled.Push('a'));
	CommandExpect(cancelled.Push('b'));
	CommandExpect(cancelled.Push('c'));
	CommandExpect(cancelled.Push(0x03));
	CommandExpect(!cancelled.Push(0x03));
	CommandExpect(cancelled.RefuseInterrupt());

	CommandExpect(cancelled.DiscardTo(0x03));
	CommandExpect(cancelled.InterruptWaiting());
	CommandExpect(cancelled.InterruptDue());
	CommandExpect(!cancelled.InterruptDue());
	CommandExpect(!cancelled.InterruptWaiting());
	CommandExpect(!cancelled.Pop(byte));

	CommandRing<4> after{start};
	CommandExpect(after.Push('a'));
	CommandExpect(after.Push('b'));
	CommandExpect(after.RefuseInterrupt());
	CommandExpect(after.Push(0x03));
	CommandExpect(after.Push('c'));

	CommandExpect(!after.DiscardTo(0x03));
	CommandExpect(after.InterruptWaiting());
	CommandExpect(after.Pop(byte) && byte == 'a');
	CommandExpect(!after.InterruptDue());
	CommandExpect(after.Pop(byte) && byte == 'b');
	CommandExpect(after.InterruptDue());
	CommandExpect(after.Pop(byte) && byte == 0x03);
	CommandExpect(after.Pop(byte) && byte == 'c');
	CommandExpect(!after.InterruptWaiting());
	CommandExpect(!after.Pop(byte));
}


// The producer and the consumer run on two threads, as Feed and Process do. The producer feeds three bytes and then a Ctrl+C into a
// ring of Size bytes, so the consumer often falls behind and the presses are refused. Each refused press that is not dropped records the
// number of bytes accepted before it. The consumer calls InterruptDue before each Pop and records the position of each Interrupt. The
// two lists must match in order, with no extra Interrupt. The verdict does not depend on the timing; the counts only show how often each
// path ran. The ring starts at start, so with nearWrap the positions cross the wrap within the first bytes.
template<std::size_t Size>
void TestRefusedInterruptsAcrossThreads(uint32 start) {
	constexpr uint32 rounds = 300000;

	CommandRing<Size> ring{start};
	std::vector<std::size_t> places;
	std::vector<std::size_t> fires;
	std::vector<uint8> sent;
	std::vector<uint8> received;
	uint32 refused = 0;
	uint32 dropped = 0;
	std::atomic<bool> producerDone{false};

	std::thread producer([&]() {
		std::size_t accepted = 0;

		for (uint32 round = 0; round < rounds; round++) {
			if (ring.Push('a')) {
				accepted++;
				sent.push_back('a');
			}

			if (ring.Push('b')) {
				accepted++;
				sent.push_back('b');
			}

			if (ring.Push('c')) {
				accepted++;
				sent.push_back('c');
			}

			if (ring.Push(0x03)) {
				accepted++;
				sent.push_back(0x03);
			} else {
				refused++;

				if (ring.RefuseInterrupt()) {
					places.push_back(accepted);
				} else {
					dropped++;

					if (dropped % 8 == 0) {
						std::this_thread::yield();
					}
				}
			}
		}

		producerDone.store(true, std::memory_order_release);
	});

	std::thread consumer([&]() {
		std::size_t popped = 0;
		uint8 byte = 0;

		for (;;) {
			bool ended = producerDone.load(std::memory_order_acquire);

			if (ring.InterruptDue()) {
				fires.push_back(popped);
				continue;
			}

			if (ring.Pop(byte)) {
				popped++;
				received.push_back(byte);
				continue;
			}

			if (ended) {
				return;
			}

			std::this_thread::yield();
		}
	});

	producer.join();
	consumer.join();

	uint8 byte = 0;

	CommandExpect(fires == places);
	CommandExpect(received == sent);
	CommandExpect(!ring.InterruptWaiting());
	CommandExpect(!ring.Pop(byte));

	std::printf("ring %zu from %u: refused presses %u, dropped %u, interrupts %zu\n", Size, start, refused, dropped, fires.size());
}


int main() {
	TestSingleThread(0);
	TestSingleThread(nearWrap);
	TestRefusedInterruptAcrossWrap();
	TestTakeWaitingAcrossWrap();
	TestDiscardAcrossWrap();

	TestRefusedInterruptsAcrossThreads<4>(0);
	TestRefusedInterruptsAcrossThreads<2>(0);
	TestRefusedInterruptsAcrossThreads<4>(nearWrap);
	TestRefusedInterruptsAcrossThreads<2>(nearWrap);

	return CommandSim::Finish("TestCommandRing");
}
