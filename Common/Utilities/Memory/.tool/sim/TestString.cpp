#include <cstdint>
#include <string_view>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/String.h>
#include "SimCheck.h"

namespace {

// A String with no allocator is empty. Its text is still terminated, and every growing call returns false.
void StringWithoutAllocatorRefuses() {
    Memory::String text("hello");
    SIM_CHECK(text.Empty());
    SIM_CHECK(!text.Append(" world"));
    SIM_CHECK(!text.Append('!'));
    SIM_CHECK(!text.Insert(0, "x"));
    SIM_CHECK(!text.Insert(0, 'x'));
    SIM_CHECK(text.Data()[0] == '\0');

    Memory::String sum = text + "tail";
    SIM_CHECK(sum.Empty());

    Memory::String copy(text);
    SIM_CHECK(copy.Empty());
}

// A String with an allocator keeps its text in a block of that allocator and returns a view of it.
void StringKeepsTextInItsAllocator() {
    static uint8_t buffer[512];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);

    Memory::String text("abc", &adapter);
    SIM_CHECK(text.Length() == 3);
    SIM_CHECK(text.Append("def"));
    SIM_CHECK(text.Length() == 6);
    SIM_CHECK(text.Find('d') == 3);
    SIM_CHECK(text.ToView() == std::string_view("abcdef"));
    SIM_CHECK(heap.UsedBytes() > 0);
}

}

int main() {
    StringWithoutAllocatorRefuses();
    StringKeepsTextInItsAllocator();
    return VstdSim::Finish("TestString");
}
