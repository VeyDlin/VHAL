#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/Storage.h>
#include "SimCheck.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

// Host tests of the storage policies: Inside, External and FromHeap. The heap of the FromHeap cases is a static object with
// external linkage, as a container's policy needs.


#define ST_EXPECT(condition) SIM_CHECK(static_cast<bool>(condition))


alignas(8) std::uint8_t storageTestArena[512];
Memory::Heap<> storageTestHeap(storageTestArena, sizeof(storageTestArena));

alignas(8) std::uint8_t storageTinyArena[64];
Memory::Heap<> storageTinyHeap(storageTinyArena, sizeof(storageTinyArena));


namespace {


void InsideCase() {
	Memory::Inside::Storage<std::uint16_t, 4> storage{};
	ST_EXPECT(storage.Ready());
	ST_EXPECT(storage.Data() != nullptr);
	ST_EXPECT(sizeof(storage) == 4 * sizeof(std::uint16_t));

	const Memory::Inside::Storage<std::uint16_t, 4>& view = storage;
	ST_EXPECT(view.Data() == storage.Data());

	storage.Data()[3] = 7;
	ST_EXPECT(view.Data()[3] == 7);
}


void ExternalCase() {
	std::uint8_t memory[8] = {};

	Memory::External::Storage<std::uint8_t, 8> fits{std::span<std::uint8_t>(memory)};
	ST_EXPECT(fits.Ready());
	ST_EXPECT(fits.Data() == memory);

	Memory::External::Storage<std::uint8_t, 9> tooSmall{std::span<std::uint8_t>(memory)};
	ST_EXPECT(!tooSmall.Ready());

	std::uint8_t larger[12] = {};
	Memory::External::Storage<std::uint8_t, 8> bigger{std::span<std::uint8_t>(larger)};
	ST_EXPECT(bigger.Ready());
	ST_EXPECT(bigger.Data() == larger);
}


void FromHeapCase() {
	using Block = Memory::FromHeap<storageTestHeap>::Storage<std::uint32_t, 4>;

	std::size_t usedBefore = storageTestHeap.UsedBytes();
	{
		Block block;
		ST_EXPECT(block.Ready());
		ST_EXPECT(block.Data() != nullptr);
		ST_EXPECT(reinterpret_cast<std::uintptr_t>(block.Data()) % alignof(std::uint32_t) == 0);
		ST_EXPECT(storageTestHeap.UsedBytes() >= usedBefore + 4 * sizeof(std::uint32_t));

		block.Data()[0] = 0x12345678u;
		std::uint32_t* before = block.Data();
		storageTestHeap.Defragment();
		ST_EXPECT(block.Data() == before);
		ST_EXPECT(block.Data()[0] == 0x12345678u);
	}
	ST_EXPECT(storageTestHeap.UsedBytes() == usedBefore);
}


void FromHeapAlignmentCase() {
	Memory::FromHeap<storageTestHeap>::Storage<std::uint8_t, 3> bytes;
	Memory::FromHeap<storageTestHeap>::Storage<std::uint64_t, 2> words;
	ST_EXPECT(bytes.Ready());
	ST_EXPECT(words.Ready());
	ST_EXPECT(reinterpret_cast<std::uintptr_t>(words.Data()) % alignof(std::uint64_t) == 0);
}


void FromHeapMoveCase() {
	using Block = Memory::FromHeap<storageTestHeap>::Storage<std::uint32_t, 4>;

	std::size_t usedBefore = storageTestHeap.UsedBytes();
	{
		Block source;
		std::uint32_t* block = source.Data();
		block[1] = 0xCAFEu;

		Block moved(std::move(source));
		ST_EXPECT(moved.Ready());
		ST_EXPECT(moved.Data() == block);
		ST_EXPECT(moved.Data()[1] == 0xCAFEu);
		ST_EXPECT(!source.Ready());
		ST_EXPECT(source.Data() == nullptr);

		Block assigned;
		std::size_t usedWithTwo = storageTestHeap.UsedBytes();
		assigned = std::move(moved);
		ST_EXPECT(assigned.Ready());
		ST_EXPECT(assigned.Data() == block);
		ST_EXPECT(!moved.Ready());
		ST_EXPECT(storageTestHeap.UsedBytes() < usedWithTwo);
	}
	ST_EXPECT(storageTestHeap.UsedBytes() == usedBefore);
}


void FromHeapExhaustedCase() {
	Memory::FromHeap<storageTinyHeap>::Storage<std::uint8_t, 4096> large;
	ST_EXPECT(!large.Ready());
	ST_EXPECT(large.Data() == nullptr);
}


}


int main() {
	InsideCase();
	ExternalCase();
	FromHeapCase();
	FromHeapAlignmentCase();
	FromHeapMoveCase();
	FromHeapExhaustedCase();

	return VstdSim::Finish("TestStorage");
}
