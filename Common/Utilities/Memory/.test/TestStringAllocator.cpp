#include <VhalCheck.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/String.h>

#include "AllocatorDoubles.h"
#include "SimCheck.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


// Host tests of the allocator that String uses for a substring and for a copy of its own text: the allocator of the output
// string when it has one, the allocator of the source string otherwise, and the refusal of each.


namespace {

// True when s holds exactly the text, with its terminator in place.
bool TextIs(const Memory::String<>& s, const char* text) {
	const std::size_t expectedLength = std::strlen(text);
	if (s.Length() != expectedLength) {
		return false;
	}
	if (s.Data()[s.Length()] != '\0') {
		return false;
	}
	return std::memcmp(s.Data(), text, expectedLength) == 0;
}

}


void SubstrUsesOutputAllocatorWhenSourceRefuses() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator sourceAlloc(inner);
	MallocAllocator outAlloc;
	Memory::String<> s("abcdef", &sourceAlloc);
	Memory::String<> out(&outAlloc);

	sourceAlloc.refusing = true;
	VHAL_CHECK(s.Substr(1, out, 3));
	VHAL_CHECK(TextIs(out, "bcd"));
}


void SubstrIntoOutputWithoutAllocatorUsesSourceAllocator() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("abcdef", &alloc);
	Memory::String<> out;

	alloc.refusing = true;
	VHAL_CHECK(!s.Substr(1, out, 3));
	VHAL_CHECK(out.Empty());

	alloc.refusing = false;
	VHAL_CHECK(s.Substr(1, out, 3));
	VHAL_CHECK(TextIs(out, "bcd"));
}


void SubstrRefusedKeepsOutputText() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator outAlloc(inner);
	MallocAllocator sourceAlloc;
	Memory::String<> s("abcdef", &sourceAlloc);
	Memory::String<> out("xyz", &outAlloc);

	outAlloc.refusing = true;
	VHAL_CHECK(!s.Substr(1, out, 3));
	VHAL_CHECK(TextIs(out, "xyz"));
	VHAL_CHECK(TextIs(s, "abcdef"));
}


void SubstrIntoItselfWithWorkingAllocator() {
	MallocAllocator heap;
	Memory::String<> s("abcdef", &heap);

	VHAL_CHECK(s.Substr(2, s, 3));
	VHAL_CHECK(TextIs(s, "cde"));
}


void AppendOwnPartRefusedKeepsText() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("abcdef", &alloc);

	alloc.refusing = true;
	VHAL_CHECK(!s.Append(s.Data() + 1));
	VHAL_CHECK(TextIs(s, "abcdef"));
}


void InsertOwnPartWithWorkingAllocator() {
	MallocAllocator heap;
	Memory::String<> s("abcdef", &heap);

	VHAL_CHECK(s.Insert(0, s.Data() + 4));
	VHAL_CHECK(TextIs(s, "efabcdef"));
}


int main() {
	SubstrUsesOutputAllocatorWhenSourceRefuses();
	SubstrIntoOutputWithoutAllocatorUsesSourceAllocator();
	SubstrRefusedKeepsOutputText();
	SubstrIntoItselfWithWorkingAllocator();
	AppendOwnPartRefusedKeepsText();
	InsertOwnPartWithWorkingAllocator();

	return VhalCheck::Finish("TestStringAllocator");
}
