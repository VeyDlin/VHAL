#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Memory/IAllocator.h>
#include <string_view>


// A growable byte string held in a block from an IAllocator. The block can move when the heap defragments, so every
// access goes through the handle again. A view returned by View() is valid until the next call that can allocate or
// run a heap step.
class HttpByteBuffer {
private:
	static constexpr uint32 MinimumCapacity = 16;

	Memory::IAllocator& allocator;
	Memory::Handle handle;
	uint32 size = 0;

public:
	explicit HttpByteBuffer(Memory::IAllocator& memory)
		: allocator(memory) {
	}


	~HttpByteBuffer() {
		Release();
	}


	HttpByteBuffer(const HttpByteBuffer&) = delete;
	HttpByteBuffer& operator=(const HttpByteBuffer&) = delete;


	// Appends one byte. outOfRange when the buffer already holds limit bytes; outOfMemory when the heap cannot grow it.
	ResultStatus Append(uint8 byte, uint32 limit) {
		if (size >= limit) {
			return ResultStatus::outOfRange;
		}

		if (size >= Capacity()) {
			uint32 wanted = (Capacity() == 0) ? MinimumCapacity : Capacity() * 2;
			if (wanted > limit) {
				wanted = limit;
			}

			Memory::Handle grown = allocator.Reallocate(handle, wanted);
			if (!grown.IsValid()) {
				return ResultStatus::outOfMemory;
			}

			handle = grown;
		}

		uint8 *bytes = static_cast<uint8 *>(handle.Data());
		bytes[size] = byte;
		size++;
		return ResultStatus::ok;
	}


	void Clear() {
		size = 0;
	}


	void RemoveLast() {
		if (size > 0) {
			size--;
		}
	}


	// Exchanges contents with other. Both must use the same allocator.
	void Swap(HttpByteBuffer& other) {
		Memory::Handle tmpHandle = handle;
		handle = other.handle;
		other.handle = tmpHandle;

		uint32 tmpSize = size;
		size = other.size;
		other.size = tmpSize;
	}


	void Release() {
		if (handle.IsValid()) {
			allocator.Deallocate(handle);
		}

		handle = Memory::Handle();
		size = 0;
	}


	uint32 Size() const {
		return size;
	}


	std::string_view View() const {
		if (!handle.IsValid()) {
			return std::string_view();
		}

		return std::string_view(static_cast<const char *>(handle.Data()), size);
	}


private:
	uint32 Capacity() const {
		if (!handle.IsValid()) {
			return 0;
		}

		return static_cast<uint32>(handle.Size());
	}
};
