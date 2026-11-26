#pragma once

#include <Utilities/Memory/Handle.h>
#include <Utilities/Memory/IAllocator.h>
#include <cstddef>
#include <cstdint>
#include <string.h>
#include <new>
#include <type_traits>
#include <utility>

namespace Memory {

// AllocatorType is the heap type (Heap<>, PoolAllocator<...>) or IAllocator for run-time choice. A heap type is called
// directly; IAllocator costs one virtual call per operation.
template<typename T, typename AllocatorType = IAllocator>
class Vector {
public:
	using ValueType = T;
	using Iterator = T*;
	using ConstIterator = const T*;

private:
	Handle handle_;
	std::size_t size_ = 0;
	std::size_t capacity_ = 0;
	AllocatorType* allocator_ = nullptr;

public:
	Vector() = default;

	explicit Vector(std::type_identity_t<AllocatorType>* alloc) : allocator_(alloc) {}

	~Vector() {
		DestroyElements();
		ReleaseBuffer();
	}


	// Copy constructor. Cannot report failure: when no allocator is available, or the allocator refuses memory,
	// the result is empty. Use an empty vector and Assign for a checked copy.
	Vector(const Vector& other) : allocator_(other.allocator_) {
		if (other.size_ > 0) {
			AllocatorType* alloc = GetAlloc();
			if (alloc != nullptr) {
				handle_ = alloc->Allocate(other.size_ * sizeof(T));
			}
			if (handle_.IsValid()) {
				allocator_ = alloc;
				capacity_ = other.size_;
				T* dst = RawData();
				const T* src = other.RawData();
				for (std::size_t i = 0; i < other.size_; i++) {
					new (&dst[i]) T(src[i]);
				}
				size_ = other.size_;
			}
		}
	}

	// Move constructor
	Vector(Vector&& other) noexcept
		: handle_(other.handle_), size_(other.size_),
		  capacity_(other.capacity_), allocator_(other.allocator_) {
		other.handle_ = Handle{};
		other.size_ = 0;
		other.capacity_ = 0;
	}

	// Copy assignment. Returns nothing, so a refused allocation is silent: use Assign for a checked copy.
	Vector& operator=(const Vector& other) {
		Assign(other);
		return *this;
	}

	// Copies other into this vector and adopts its allocator. Returns false, with this vector unchanged,
	// when the allocator refuses memory.
	bool Assign(const Vector& other) {
		if (this == &other) {
			return true;
		}

		// The new buffer is allocated and filled before anything of this vector is touched.
		Handle fresh;
		AllocatorType* newAlloc = nullptr;
		if (other.size_ > 0) {
			newAlloc = other.GetAlloc();
			if (newAlloc == nullptr) {
				return false;
			}
			fresh = newAlloc->Allocate(other.size_ * sizeof(T));
			if (!fresh.IsValid()) {
				return false;
			}
			T* dst = static_cast<T*>(fresh.Data());
			const T* src = other.RawData();
			for (std::size_t i = 0; i < other.size_; i++) {
				new (&dst[i]) T(src[i]);
			}
		}

		DestroyElements();
		ReleaseBuffer();
		handle_ = fresh;
		size_ = other.size_;
		capacity_ = other.size_;
		allocator_ = newAlloc != nullptr ? newAlloc : other.allocator_;
		return true;
	}

	// Move assignment
	Vector& operator=(Vector&& other) noexcept {
		if (this != &other) {
			DestroyElements();
			ReleaseBuffer();
			handle_ = other.handle_;
			size_ = other.size_;
			capacity_ = other.capacity_;
			allocator_ = other.allocator_;
			other.handle_ = Handle{};
			other.size_ = 0;
			other.capacity_ = 0;
		}
		return *this;
	}


	// Growing operations return false, and leave the vector unchanged, when the allocator refuses memory.
	bool PushBack(const T& value) {
		// value may refer into our own buffer, which Grow can free, so it is copied first
		return PushBack(T(value));
	}

	// When the vector is full and value lies in its buffer, value is moved out before the buffer is freed. A refused
	// growth then leaves value moved from.
	bool PushBack(T&& value) {
		if (size_ >= capacity_) {
			if (InOwnBuffer(&value)) {
				T moved(static_cast<T&&>(value));
				return PushBack(static_cast<T&&>(moved));
			}
			if (!Grow(size_ + 1)) {
				return false;
			}
		}

		T* data = RawData();
		new (&data[size_]) T(static_cast<T&&>(value));
		size_++;
		return true;
	}

	// Returns the new element, or nullptr when the allocator refuses memory. When the vector is full, the element is
	// built before the buffer is grown, because an argument may refer into the buffer that growing frees.
	template<typename... Args>
	T* EmplaceBack(Args&&... args) {
		if (size_ >= capacity_) {
			T element(static_cast<Args&&>(args)...);
			if (!PushBack(static_cast<T&&>(element))) {
				return nullptr;
			}
			return RawData() + size_ - 1;
		}

		T* data = RawData();
		T* ptr = new (&data[size_]) T(static_cast<Args&&>(args)...);
		size_++;
		return ptr;
	}

	void PopBack() {
		if (size_ > 0) {
			size_--;
			if constexpr (!std::is_trivially_destructible_v<T>) {
				RawData()[size_].~T();
			}
		}
	}

	// Returns false for an index past the end, as well as when the allocator refuses memory.
	bool Insert(std::size_t index, const T& value) {
		if (index > size_) {
			return false;
		}
		T copy(value);
		if (size_ >= capacity_ && !Grow(size_ + 1)) {
			return false;
		}
		T* data = RawData();
		for (std::size_t i = size_; i > index; i--) {
			new (&data[i]) T(static_cast<T&&>(data[i - 1]));
			data[i - 1].~T();
		}
		new (&data[index]) T(static_cast<T&&>(copy));
		size_++;
		return true;
	}

	void Erase(std::size_t index) {
		if (index >= size_) return;
		T* data = RawData();
		data[index].~T();
		for (std::size_t i = index; i + 1 < size_; i++) {
			new (&data[i]) T(static_cast<T&&>(data[i + 1]));
			data[i + 1].~T();
		}
		size_--;
	}

	void EraseRange(std::size_t first, std::size_t count) {
		if (first >= size_ || count == 0) return;
		if (first + count > size_) count = size_ - first;
		T* data = RawData();
		for (std::size_t i = first; i < first + count; i++) {
			data[i].~T();
		}
		for (std::size_t i = first; i + count < size_; i++) {
			new (&data[i]) T(static_cast<T&&>(data[i + count]));
			data[i + count].~T();
		}
		size_ -= count;
	}


	std::size_t Find(const T& value) const {
		const T* data = RawData();
		for (std::size_t i = 0; i < size_; i++) {
			if (data[i] == value) return i;
		}
		return static_cast<std::size_t>(-1);
	}

	bool Contains(const T& value) const {
		return Find(value) != static_cast<std::size_t>(-1);
	}


	T& operator[](std::size_t i) { return RawData()[i]; }
	const T& operator[](std::size_t i) const { return RawData()[i]; }


	// Pointer to the first or last element, or nullptr when the vector is empty. operator[] does not check the index.
	T* Front() {
		if (size_ == 0) {
			return nullptr;
		}
		return RawData();
	}


	const T* Front() const {
		if (size_ == 0) {
			return nullptr;
		}
		return RawData();
	}


	T* Back() {
		if (size_ == 0) {
			return nullptr;
		}
		return RawData() + size_ - 1;
	}


	const T* Back() const {
		if (size_ == 0) {
			return nullptr;
		}
		return RawData() + size_ - 1;
	}


	T* Data() { return RawData(); }
	const T* Data() const { return RawData(); }

	std::size_t Size() const { return size_; }
	std::size_t Capacity() const { return capacity_; }
	bool Empty() const { return size_ == 0; }


	void Clear() {
		DestroyElements();
	}

	// Returns false when the allocator refuses memory; the capacity is then unchanged.
	bool Reserve(std::size_t newCapacity) {
		if (newCapacity <= capacity_) {
			return true;
		}
		return Grow(newCapacity);
	}

	// Returns false when growing fails; the size and elements are then unchanged.
	bool Resize(std::size_t newSize) {
		if (newSize < size_) {
			if constexpr (!std::is_trivially_destructible_v<T>) {
				T* data = RawData();
				for (std::size_t i = newSize; i < size_; i++) {
					data[i].~T();
				}
			}
			size_ = newSize;
		} else if (newSize > size_) {
			if (!Reserve(newSize)) {
				return false;
			}
			T* data = RawData();
			for (std::size_t i = size_; i < newSize; i++) {
				new (&data[i]) T();
			}
			size_ = newSize;
		}
		return true;
	}


	Iterator begin() { return RawData(); }
	Iterator end() { return RawData() + size_; }
	ConstIterator begin() const { return RawData(); }
	ConstIterator end() const { return RawData() + size_; }

private:
	// The most elements a buffer can hold without its byte count overflowing size_t.
	static constexpr std::size_t kMaxCapacity = static_cast<std::size_t>(-1) / sizeof(T);

	// The allocator the vector was given. Returns nullptr when there is none, and then no buffer can be made.
	AllocatorType* GetAlloc() const {
		return allocator_;
	}

	// Frees the buffer to the allocator that holds it. Callers destroy the elements first.
	void ReleaseBuffer() {
		if (handle_.IsValid() && allocator_ != nullptr) {
			allocator_->Deallocate(handle_);
		}
		handle_ = Handle{};
	}

	T* RawData() const {
		if (!handle_.IsValid()) return nullptr;
		return static_cast<T*>(handle_.Data());
	}

	// True when p points into the buffer, so that growing the vector frees the memory p points to.
	bool InOwnBuffer(const T* p) const {
		if (!handle_.IsValid()) {
			return false;
		}
		std::uintptr_t address = reinterpret_cast<std::uintptr_t>(p);
		std::uintptr_t first = reinterpret_cast<std::uintptr_t>(RawData());
		return address >= first && address < first + capacity_ * sizeof(T);
	}

	// On failure the old buffer stays as it was, so the caller keeps its elements and capacity.
	bool Grow(std::size_t minCapacity) {
		if (minCapacity > kMaxCapacity) {
			return false;
		}

		std::size_t newCap = capacity_ == 0 ? 4 : (capacity_ > kMaxCapacity / 2 ? kMaxCapacity : capacity_ * 2);
		if (newCap < minCapacity) {
			newCap = minCapacity;
		}

		AllocatorType* alloc = GetAlloc();
		if (alloc == nullptr) {
			return false;
		}

		Handle newHandle = alloc->Reallocate(handle_, newCap * sizeof(T));
		if (!newHandle.IsValid()) {
			return false;
		}

		handle_ = newHandle;
		allocator_ = alloc;
		capacity_ = newCap;
		return true;
	}

	void DestroyElements() {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			T* data = RawData();
			if (data) {
				for (std::size_t i = 0; i < size_; i++) {
					data[i].~T();
				}
			}
		}
		size_ = 0;
	}
};

}
