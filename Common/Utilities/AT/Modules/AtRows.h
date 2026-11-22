#pragma once
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <new>


// The row module of the client, listed among its modules as AtRows<N>: AtClient<Set, Binding, AtQueue<2>, AtRows<8>>. It holds one list of
// up to N rows of the set's largest row type, shared by the Rows and ReadRows calls. One request is on the wire at a time, so one list
// serves every call. A client that does not list it has no row storage, and a Rows call is a compile error. RowsVisit and ReadRowsVisit
// hand each row to a handler as it arrives, and need no list.


// The module tag. N is the number of rows the list holds. Rows past N are counted in Skipped() and not stored.
template<std::size_t Capacity>
struct AtRows {
	static_assert(Capacity > 0, "AtRows needs a capacity of at least 1");

	static constexpr std::size_t capacity = Capacity;
};


// True for an AtRows tag, whatever its capacity.
template<typename Module>
inline constexpr bool AtIsRowsModule = false;


template<std::size_t Capacity>
inline constexpr bool AtIsRowsModule<AtRows<Capacity>> = true;


// The number of AtRows tags in a list of modules.
template<typename... Modules>
constexpr std::size_t AtRowsModuleCount() {
	return ((AtIsRowsModule<Modules> ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
}


// The capacity of the AtRows tag in a list of modules, or zero when the list has none.
template<typename... Modules>
struct AtRowsOf {
	static constexpr std::size_t capacity = 0;
};


template<std::size_t Capacity, typename... Rest>
struct AtRowsOf<AtRows<Capacity>, Rest...> {
	static constexpr std::size_t capacity = AtRows<Capacity>::capacity;
};


template<typename Head, typename... Rest>
struct AtRowsOf<Head, Rest...> : AtRowsOf<Rest...> {
};


// The list of rows that a Rows or ReadRows handler gets. It views the client's row storage, so it is valid while the handler runs. Rows
// are stored one after another, each as the row type of the call. The index must be below Count().
template<typename Row, std::size_t Capacity>
class AtRowList {
public:
	static constexpr std::size_t capacity = Capacity;


private:
	const std::byte* rowBytes = nullptr;
	std::size_t rowCount = 0;
	std::size_t skippedCount = 0;


public:
	AtRowList(const std::byte* storage, std::size_t count, std::size_t skipped) : rowBytes(storage), rowCount(count), skippedCount(skipped) {
	}


	// The number of rows that were stored, up to the capacity.
	std::size_t Count() const {
		return rowCount;
	}


	// The number of rows that came past the capacity and were not stored.
	std::size_t Skipped() const {
		return skippedCount;
	}


	const Row& operator[](std::size_t index) const {
		return *std::launder(reinterpret_cast<const Row*>(rowBytes + index * sizeof(Row)));
	}


	const Row* begin() const {
		return std::launder(reinterpret_cast<const Row*>(rowBytes));
	}


	const Row* end() const {
		return begin() + rowCount;
	}
};


// The rows of an AwaitRows or AwaitReadRows call: a copy that the caller owns, so it stays valid after the call returns. It holds up to Capacity
// rows of Row, and counts the rows past the capacity. The copy is made when the request finishes, from the storage of the client.
template<typename Row, std::size_t Capacity>
class AtRowCopy {
public:
	static constexpr std::size_t capacity = Capacity;


private:
	std::array<Row, Capacity> rowArray{};
	std::size_t rowCount = 0;
	std::size_t skippedCount = 0;


public:
	AtRowCopy() = default;


	AtRowCopy(const std::byte* storage, std::size_t count, std::size_t skipped) : rowCount(count), skippedCount(skipped) {
		for (std::size_t index = 0; index < count; index++) {
			rowArray[index] = *std::launder(reinterpret_cast<const Row*>(storage + index * sizeof(Row)));
		}
	}


	// The number of rows that were stored, up to the capacity.
	std::size_t Count() const {
		return rowCount;
	}


	// The number of rows that came past the capacity and were not stored.
	std::size_t Skipped() const {
		return skippedCount;
	}


	const Row& operator[](std::size_t index) const {
		return rowArray[index];
	}


	const Row* begin() const {
		return rowArray.data();
	}


	const Row* end() const {
		return rowArray.data() + rowCount;
	}
};


// The row storage of a client that lists AtRows<N>: N rows of the set's largest row type, and the counts of the current reply.
template<std::size_t Bytes>
struct AtRowStorage {
	alignas(8) std::byte bytes[Bytes]{};
	std::size_t count = 0;
	std::size_t skipped = 0;
};


// The row storage of a client without AtRows: it has no bytes, and takes no room in the client.
struct AtNoRowStorage {
};
