#include <VhalCheck.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/String.h>

#include "AllocatorDoubles.h"
#include "SimCheck.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>


// Host tests of String editing and of String under a refusing allocator: erase, substring, appending the string to itself, and
// the text kept when a growing operation is refused.


namespace {

// True when s holds exactly the text, with its terminator in place. Indexing Data()[Length()] is in bounds
// whenever the terminator is there, so a broken string fails the check rather than reading past its buffer.
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


void StringBasicOperations() {
	MallocAllocator heap;
	Memory::String<> s("hello", &heap);
	VHAL_CHECK_EQ(s.Length(), static_cast<std::size_t>(5));
	s.Append(" world");
	VHAL_CHECK_EQ(s.ToView(), std::string_view("hello world"));
	VHAL_CHECK_EQ(s.Find("world"), static_cast<std::size_t>(6));
	Memory::String<> head(&heap);
	VHAL_CHECK(s.Substr(0, head, 5));
	VHAL_CHECK_EQ(head.ToView(), std::string_view("hello"));
	VHAL_CHECK(s.StartsWith("hell"));
	VHAL_CHECK(s.EndsWith("world"));
	s.Insert(0, ">> ");
	VHAL_CHECK_EQ(s.ToView(), std::string_view(">> hello world"));
}


void StringEraseWithNposCountRemovesToEnd() {
	MallocAllocator heap;
	Memory::String<> s("abcdef", &heap);
	s.Erase(1, Memory::String<>::Npos);
	VHAL_CHECK_EQ(s.Length(), static_cast<std::size_t>(1));
	VHAL_CHECK_EQ(s.ToView(), std::string_view("a"));
}


void StringSubstrCountOverflowIsClamped() {
	MallocAllocator heap;
	Memory::String<> s("abcdef", &heap);
	Memory::String<> part(&heap);
	VHAL_CHECK(s.Substr(2, part, Memory::String<>::Npos - 1));
	VHAL_CHECK_EQ(part.ToView(), std::string_view("cdef"));
}


void StringAppendOfItself() {
	MallocAllocator heap;
	Memory::String<> s("abcdefg", &heap);
	s.Append(s);
	VHAL_CHECK_EQ(s.ToView(), std::string_view("abcdefgabcdefg"));
}


void StringAppendStringFailureKeepsContent() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("abc", &alloc);

	alloc.refusing = true;
	s.Append("defgh");

	VHAL_CHECK(TextIs(s, "abc"));
}


void StringAppendCharFailureKeepsContent() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("abc", &alloc);

	alloc.refusing = true;
	s.Append('d');

	VHAL_CHECK(TextIs(s, "abc"));
}


void StringInsertFailureMidwayKeepsContent() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	// Append grows the buffer to capacity 8 holding five elements: three inserted characters fit, the fourth does not.
	Memory::String<> s("ab", &alloc);
	s.Append("cd");

	alloc.refusing = true;
	s.Insert(0, "xyzw");

	VHAL_CHECK(TextIs(s, "abcd"));
}


void StringInsertCharFailureKeepsContent() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("abc", &alloc);

	alloc.refusing = true;
	s.Insert(1, 'x');

	VHAL_CHECK(TextIs(s, "abc"));
}


void StringConstructFailureYieldsEmpty() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);

	alloc.refusing = true;
	Memory::String<> s("abcdef", &alloc);

	VHAL_CHECK(TextIs(s, ""));
}


void StringCopyAssignFailureKeepsOldContent() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> source("source text", &alloc);
	Memory::String<> target("xy", &alloc);

	alloc.refusing = true;
	target = source;

	VHAL_CHECK(TextIs(target, "xy"));
}


void StringCheckedOperationsReportFailureAndKeepText() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);

	// Full buffers (capacity 4 holding "abc"): every growing operation needs an allocation.
	Memory::String<> appendChar("abc", &alloc);
	alloc.refusing = true;
	VHAL_CHECK(!appendChar.Append('d'));
	alloc.refusing = false;
	VHAL_CHECK(TextIs(appendChar, "abc"));

	Memory::String<> appendText("abc", &alloc);
	alloc.refusing = true;
	VHAL_CHECK(!appendText.Append("d"));
	alloc.refusing = false;
	VHAL_CHECK(TextIs(appendText, "abc"));

	Memory::String<> appendSelf("abc", &alloc);
	alloc.refusing = true;
	VHAL_CHECK(!appendSelf.Append(appendSelf));
	alloc.refusing = false;
	VHAL_CHECK(TextIs(appendSelf, "abc"));

	Memory::String<> insertChar("abc", &alloc);
	alloc.refusing = true;
	VHAL_CHECK(!insertChar.Insert(1, 'x'));
	alloc.refusing = false;
	VHAL_CHECK(TextIs(insertChar, "abc"));

	Memory::String<> insertText("abc", &alloc);
	alloc.refusing = true;
	VHAL_CHECK(!insertText.Insert(0, "x"));
	alloc.refusing = false;
	VHAL_CHECK(TextIs(insertText, "abc"));

	Memory::String<> source("source", &alloc);
	Memory::String<> assignTarget("xy", &alloc);
	alloc.refusing = true;
	VHAL_CHECK(!assignTarget.Assign(source));
	alloc.refusing = false;
	VHAL_CHECK(TextIs(assignTarget, "xy"));
	VHAL_CHECK(TextIs(source, "source"));
}


void StringIndexPastEndReportsFalse() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("ab", &alloc);
	VHAL_CHECK(s.Insert(2, 'x'));
	VHAL_CHECK(!s.Insert(10, 'y'));
	VHAL_CHECK(!s.Insert(10, "y"));
	VHAL_CHECK(TextIs(s, "abx"));
}


// A refused substring leaves the output and the source unchanged. Substr builds its result without an allocator, so it refuses
// a non-empty substring whatever the state of the allocator; the check holds for that reason too.
void StringSubstrFailureKeepsOutUnchanged() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("abcdef", &alloc);
	Memory::String<> out("xyz", &alloc);

	alloc.refusing = true;
	VHAL_CHECK(!s.Substr(1, out, 3));
	VHAL_CHECK(TextIs(out, "xyz"));
	VHAL_CHECK(TextIs(s, "abcdef"));
}


void StringInsertOwnSubrangeOnSuccess() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::String<> s("ab", &alloc);
	VHAL_CHECK(s.Append("cd"));

	// The source lies inside the buffer that the insertion rewrites.
	VHAL_CHECK(s.Insert(0, s.Data() + 2));
	VHAL_CHECK(TextIs(s, "cdabcd"));
}


int main() {
	StringBasicOperations();
	StringEraseWithNposCountRemovesToEnd();
	StringSubstrCountOverflowIsClamped();
	StringAppendOfItself();
	StringAppendStringFailureKeepsContent();
	StringAppendCharFailureKeepsContent();
	StringInsertFailureMidwayKeepsContent();
	StringInsertCharFailureKeepsContent();
	StringConstructFailureYieldsEmpty();
	StringCopyAssignFailureKeepsOldContent();
	StringCheckedOperationsReportFailureAndKeepText();
	StringIndexPastEndReportsFalse();
	StringSubstrFailureKeepsOutUnchanged();
	StringInsertOwnSubrangeOnSuccess();

	return VhalCheck::Finish("TestStringEdit");
}
