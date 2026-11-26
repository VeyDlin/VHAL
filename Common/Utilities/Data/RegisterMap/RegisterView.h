#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/CompileTime/Require.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>


// A view of user data as Modbus tables. The view owns no storage: each entry points at a user variable, or at functions that
// read and write a value. The view is a constexpr list, built by a captureless lambda:
//
//   extern uint16 setpoint;
//   bool CheckSetpoint(uint16 value);
//   uint16 ReadChipTemperature();
//
//   using Table = RegisterView<+[] {
//       return RegisterViewBuilder()
//           .HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
//           .InputRegisters(0x0010, Computed(&ReadChipTemperature))
//           .Coils(0x0000, Variable(&relayState));
//   }>;
//
// Entry rules, checked at compile time:
//   - holding and input entries take integers of 2, 4 or 8 bytes (a value of N words takes N registers, the most significant
//     word first, each word big-endian) and arrays of 16-bit integers (one element per register);
//   - coil and discrete entries take bool or std::array<bool, N> (one coil per bool, in RAM);
//   - two entries of one table may not share an address, and no block may run past Modbus address 0xFFFF;
//   - input registers and discrete inputs are read only: a Computed with a set function is an error there.
//
// The static functions are the lookup the Modbus server uses. A write is all or nothing: the whole request is checked first,
// and nothing is stored unless every entry passes the check pass, which runs over all entries before the write pass.
//
// An array entry takes a write of part of it: the entry's current value is read, the covered elements are replaced, and the
// merged value is checked and stored. The entry is read once in the check pass and once in the write pass. The second read keeps
// an uncovered element that the device changed in between (an interrupt, another task), so the stored value can differ from the
// checked one in those elements only. The merge builds a temporary the size of the entry's array on the stack. A number or a
// single bool is always written whole.


// The traits of an array value, such as std::array<uint16, 4> or std::array<bool, 8>.
template<typename Type>
struct RegisterViewArray : std::false_type {
};


template<typename Element, std::size_t Size>
struct RegisterViewArray<std::array<Element, Size>> : std::true_type {
	using ElementType = Element;
	static constexpr std::size_t size = Size;
};


// True for the value types a holding or input entry takes.
template<typename Value>
constexpr bool RegisterViewIsWordValue() {
	if constexpr (RegisterViewArray<Value>::value) {
		return RegisterViewArray<Value>::size >= 1 && std::is_integral_v<typename RegisterViewArray<Value>::ElementType>
			&& !std::is_same_v<typename RegisterViewArray<Value>::ElementType, bool>
			&& sizeof(typename RegisterViewArray<Value>::ElementType) == 2;
	} else {
		return std::is_integral_v<Value> && !std::is_same_v<Value, bool> && (sizeof(Value) == 2 || sizeof(Value) == 4 || sizeof(Value) == 8);
	}
}


// True for the value types a coil or discrete entry takes.
template<typename Value>
constexpr bool RegisterViewIsBitValue() {
	if constexpr (std::is_same_v<Value, bool>) {
		return true;
	} else if constexpr (RegisterViewArray<Value>::value) {
		return RegisterViewArray<Value>::size >= 1 && std::is_same_v<typename RegisterViewArray<Value>::ElementType, bool>;
	} else {
		return false;
	}
}


// The number of registers or coils a value takes.
template<typename Value>
constexpr std::size_t RegisterViewCount() {
	if constexpr (std::is_same_v<Value, bool>) {
		return 1;
	} else if constexpr (RegisterViewArray<Value>::value) {
		return RegisterViewArray<Value>::size;
	} else {
		return sizeof(Value) / 2;
	}
}


// The 16-bit words of a value, most significant first for an integer, one element per word for an array.
template<typename Value>
std::array<uint16, RegisterViewCount<Value>()> RegisterViewToWords(Value value) {
	std::array<uint16, RegisterViewCount<Value>()> words{};
	if constexpr (RegisterViewArray<Value>::value) {
		for (std::size_t index = 0; index < words.size(); index++) {
			words[index] = static_cast<uint16>(value[index]);
		}
	} else {
		using Unsigned = std::make_unsigned_t<Value>;
		Unsigned raw = static_cast<Unsigned>(value);
		for (std::size_t index = 0; index < words.size(); index++) {
			words[index] = static_cast<uint16>(raw >> (16 * (words.size() - 1 - index)));
		}
	}
	return words;
}


// The value from its words, in the order RegisterViewToWords gives. words holds exactly RegisterViewCount<Value>() words.
template<typename Value>
Value RegisterViewFromWords(std::span<const uint16> words) {
	Value value{};
	if constexpr (RegisterViewArray<Value>::value) {
		for (std::size_t index = 0; index < words.size(); index++) {
			value[index] = static_cast<typename RegisterViewArray<Value>::ElementType>(words[index]);
		}
	} else {
		using Unsigned = std::make_unsigned_t<Value>;
		Unsigned raw = 0;
		for (std::size_t index = 0; index < words.size(); index++) {
			raw = static_cast<Unsigned>((raw << 16) | words[index]);
		}
		value = static_cast<Value>(raw);
	}
	return value;
}


// The coils of a value, one bool per coil.
template<typename Value>
std::array<bool, RegisterViewCount<Value>()> RegisterViewToBits(const Value& value) {
	std::array<bool, RegisterViewCount<Value>()> bits{};
	if constexpr (RegisterViewArray<Value>::value) {
		for (std::size_t index = 0; index < bits.size(); index++) {
			bits[index] = value[index];
		}
	} else {
		bits[0] = value;
	}
	return bits;
}


// The value from its coils. bits holds exactly RegisterViewCount<Value>() coils.
template<typename Value>
Value RegisterViewFromBits(std::span<const bool> bits) {
	Value value{};
	if constexpr (RegisterViewArray<Value>::value) {
		for (std::size_t index = 0; index < bits.size(); index++) {
			value[index] = bits[index];
		}
	} else {
		value = bits[0];
	}
	return value;
}


// A user variable with static storage. check is optional: it returns true for a value a write may store.
template<typename ValueType>
struct VariableSource {
	using Value = ValueType;

	static constexpr bool kWritable = true;
	static constexpr bool kWritesThroughFunction = false;

	ValueType* target;
	bool (*check)(ValueType);


	ValueType Get() const {
		return *target;
	}


	ResultStatus Set(ValueType value) const {
		*target = value;
		return ResultStatus::ok;
	}


	bool Accepts(ValueType value) const {
		return check == nullptr || check(value);
	}
};


// A value read by a function, and optionally written by one. Settable is false for a read-only source.
template<typename ValueType, bool Settable>
struct ComputedSource {
	using Value = ValueType;

	static constexpr bool kWritable = Settable;
	static constexpr bool kWritesThroughFunction = Settable;

	ValueType (*get)();
	ResultStatus (*set)(ValueType);
	bool (*check)(ValueType);


	ValueType Get() const {
		return get();
	}


	ResultStatus Set(ValueType value) const {
		if constexpr (Settable) {
			return set(value);
		} else {
			return ResultStatus::accessError;
		}
	}


	bool Accepts(ValueType value) const {
		return check == nullptr || check(value);
	}
};


// A user variable. Variable(&x) or Variable(&x, &check).
template<typename ValueType>
constexpr VariableSource<ValueType> Variable(ValueType* target) {
	Require<"RegisterMap:a variable needs a target">(target != nullptr);
	return VariableSource<ValueType>{ target, nullptr };
}


template<typename ValueType>
constexpr VariableSource<ValueType> Variable(ValueType* target, bool (*check)(ValueType)) {
	Require<"RegisterMap:a variable needs a target">(target != nullptr);
	return VariableSource<ValueType>{ target, check };
}


// A read-only function source. Computed(&get).
template<typename ValueType>
constexpr ComputedSource<ValueType, false> Computed(ValueType (*get)()) {
	Require<"RegisterMap:a computed value needs a read function">(get != nullptr);
	return ComputedSource<ValueType, false>{ get, nullptr, nullptr };
}


// A function source with a write. Computed(&get, &set) or Computed(&get, &set, &check).
template<typename ValueType>
constexpr ComputedSource<ValueType, true> Computed(ValueType (*get)(), ResultStatus (*set)(ValueType)) {
	Require<"RegisterMap:a computed value needs a read function">(get != nullptr);
	Require<"RegisterMap:a computed value needs a set function">(set != nullptr);
	return ComputedSource<ValueType, true>{ get, set, nullptr };
}


template<typename ValueType>
constexpr ComputedSource<ValueType, true> Computed(ValueType (*get)(), ResultStatus (*set)(ValueType), bool (*check)(ValueType)) {
	Require<"RegisterMap:a computed value needs a read function">(get != nullptr);
	Require<"RegisterMap:a computed value needs a set function">(set != nullptr);
	return ComputedSource<ValueType, true>{ get, set, check };
}


// The lookup data of one entry: table, first address and register or coil count. The view keeps one per entry, in a constexpr array.
struct RegisterViewSlot {
	RegisterTable table;
	bool allowsPartial;
	std::size_t address;
	std::size_t count;
};


// One entry of a view: a first address and a source, in a table. The table is a template argument.
template<RegisterTable Table, typename Source>
struct RegisterViewEntry {
	using Value = typename Source::Value;

	static constexpr RegisterTable table = Table;
	static constexpr std::size_t count = RegisterViewCount<Value>();
	static constexpr bool isWords = Table == RegisterTable::Holding || Table == RegisterTable::Input;
	static constexpr bool writable = (Table == RegisterTable::Holding || Table == RegisterTable::Coil) && Source::kWritable;
	static constexpr bool allowsPartial = RegisterViewArray<Value>::value;

	uint16 address;
	Source source;


	constexpr RegisterViewSlot Slot() const {
		return RegisterViewSlot{ table, allowsPartial, address, count };
	}


	// Copies the words from first (an address inside the entry) into out.
	ResultStatus ReadWords(std::size_t first, std::span<uint16> out) const {
		if constexpr (isWords) {
			std::array<uint16, count> words = RegisterViewToWords<Value>(source.Get());
			std::size_t offset = first - address;
			for (std::size_t index = 0; index < out.size(); index++) {
				out[index] = words[offset + index];
			}
			return ResultStatus::ok;
		} else {
			return ResultStatus::notSupported;
		}
	}


	// Copies the coils from first (an address inside the entry) into out.
	ResultStatus ReadBits(std::size_t first, std::span<bool> out) const {
		if constexpr (!isWords) {
			std::array<bool, count> bits = RegisterViewToBits<Value>(source.Get());
			std::size_t offset = first - address;
			for (std::size_t index = 0; index < out.size(); index++) {
				out[index] = bits[offset + index];
			}
			return ResultStatus::ok;
		} else {
			return ResultStatus::notSupported;
		}
	}


	// Sets, in out, the bits of the coils from first to last, which are addresses inside the entry. Bit (address - base) of out is the
	// coil at address, least significant bit first. The entry is read once. Bits that are not coils of the range are left as they are.
	ResultStatus ReadPackedBits(std::size_t first, std::size_t last, std::size_t base, std::span<uint8> out) const {
		if constexpr (!isWords) {
			std::array<bool, count> bits = RegisterViewToBits<Value>(source.Get());
			for (std::size_t position = first; position < last; position++) {
				if (bits[position - address]) {
					std::size_t bit = position - base;
					out[bit / 8] = static_cast<uint8>(out[bit / 8] | (1U << (bit % 8)));
				}
			}
			return ResultStatus::ok;
		} else {
			return ResultStatus::notSupported;
		}
	}


	// The value a write stores. The request covers the words from first (an address inside the entry). A whole entry is the request
	// alone, and source.Get is not called. An array entry that is covered in part is read once here, and the covered elements are
	// replaced in that value. A number is always covered whole, so it never takes this path.
	Value MergeWords([[maybe_unused]] std::size_t first, std::span<const uint16> words) const {
		if constexpr (RegisterViewArray<Value>::value) {
			if (words.size() == count) {
				return RegisterViewFromWords<Value>(words);
			}
			std::array<uint16, count> current = RegisterViewToWords<Value>(source.Get());
			std::size_t offset = first - address;
			for (std::size_t index = 0; index < words.size(); index++) {
				current[offset + index] = words[index];
			}
			return RegisterViewFromWords<Value>(current);
		} else {
			return RegisterViewFromWords<Value>(words);
		}
	}


	// The value a write stores, for coils. The same rule as MergeWords.
	Value MergeBits([[maybe_unused]] std::size_t first, std::span<const bool> bits) const {
		if constexpr (RegisterViewArray<Value>::value) {
			if (bits.size() == count) {
				return RegisterViewFromBits<Value>(bits);
			}
			std::array<bool, count> current = RegisterViewToBits<Value>(source.Get());
			std::size_t offset = first - address;
			for (std::size_t index = 0; index < bits.size(); index++) {
				current[offset + index] = bits[index];
			}
			return RegisterViewFromBits<Value>(current);
		} else {
			return RegisterViewFromBits<Value>(bits);
		}
	}


	// Checks the words covered from first: writable, and accepted by the check once merged. Call before WriteWords.
	ResultStatus CheckWords(std::size_t first, std::span<const uint16> words) const {
		if constexpr (isWords) {
			if (!writable) {
				return ResultStatus::accessError;
			}
			return source.Accepts(MergeWords(first, words)) ? ResultStatus::ok : ResultStatus::invalidData;
		} else {
			return ResultStatus::notSupported;
		}
	}


	ResultStatus CheckBits(std::size_t first, std::span<const bool> bits) const {
		if constexpr (!isWords) {
			if (!writable) {
				return ResultStatus::accessError;
			}
			return source.Accepts(MergeBits(first, bits)) ? ResultStatus::ok : ResultStatus::invalidData;
		} else {
			return ResultStatus::notSupported;
		}
	}


	// Stores the words covered from first. Call only after CheckWords.
	ResultStatus WriteWords(std::size_t first, std::span<const uint16> words) const {
		if constexpr (isWords) {
			if (!writable) {
				return ResultStatus::accessError;
			}
			return source.Set(MergeWords(first, words));
		} else {
			return ResultStatus::notSupported;
		}
	}


	ResultStatus WriteBits(std::size_t first, std::span<const bool> bits) const {
		if constexpr (!isWords) {
			if (!writable) {
				return ResultStatus::accessError;
			}
			return source.Set(MergeBits(first, bits));
		} else {
			return ResultStatus::notSupported;
		}
	}
};


// Builds an entry of a table and checks it against the table's rules.
template<RegisterTable Table, typename Source>
constexpr RegisterViewEntry<Table, Source> RegisterViewMake(uint16 address, Source source) {
	using Value = typename Source::Value;
	if constexpr (Table == RegisterTable::Holding || Table == RegisterTable::Input) {
		Require<"RegisterMap:a register value is an integer of 2, 4 or 8 bytes, or an array of 16-bit integers">(RegisterViewIsWordValue<Value>());
	} else {
		Require<"RegisterMap:a coil or discrete input is a bool or an array of bool">(RegisterViewIsBitValue<Value>());
	}

	Require<"RegisterMap:an entry has at least one register">(RegisterViewCount<Value>() >= 1);
	Require<"RegisterMap:a block runs past Modbus address 0xFFFF">(static_cast<std::size_t>(address) + RegisterViewCount<Value>() <= 0x10000);
	Require<"RegisterMap:input registers and discrete inputs cannot take a set function">(
		!((Table == RegisterTable::Input || Table == RegisterTable::Discrete) && Source::kWritesThroughFunction));
	return RegisterViewEntry<Table, Source>{ address, source };
}


// The entries of a view, as a list of types. The newest entry comes first. A recursive list is used rather than std::tuple,
// because GCC rejects a std::tuple built inside a captureless lambda that a constant expression calls through a function pointer.
template<typename... Entries>
struct RegisterViewList {
};


template<typename Head, typename... Tail>
struct RegisterViewList<Head, Tail...> {
	Head head;
	RegisterViewList<Tail...> tail;
};


// The fluent builder of a view. Each method returns a new builder whose type lists the entries, so the view knows every entry
// type at compile time. The builder is a constexpr value; the final list is given to RegisterView.
template<typename... Entries>
struct RegisterViewBuilder {
	static constexpr std::size_t count = sizeof...(Entries);


	RegisterViewList<Entries...> list;


	constexpr RegisterViewBuilder() = default;


	constexpr explicit RegisterViewBuilder(RegisterViewList<Entries...> value) : list(value) {
	}


	template<typename Source>
	constexpr RegisterViewBuilder<RegisterViewEntry<RegisterTable::Holding, Source>, Entries...> HoldingRegisters(uint16 address, Source source) const {
		return Append(RegisterViewMake<RegisterTable::Holding, Source>(address, source));
	}


	template<typename Source>
	constexpr RegisterViewBuilder<RegisterViewEntry<RegisterTable::Input, Source>, Entries...> InputRegisters(uint16 address, Source source) const {
		return Append(RegisterViewMake<RegisterTable::Input, Source>(address, source));
	}


	template<typename Source>
	constexpr RegisterViewBuilder<RegisterViewEntry<RegisterTable::Coil, Source>, Entries...> Coils(uint16 address, Source source) const {
		return Append(RegisterViewMake<RegisterTable::Coil, Source>(address, source));
	}


	template<typename Source>
	constexpr RegisterViewBuilder<RegisterViewEntry<RegisterTable::Discrete, Source>, Entries...> DiscreteInputs(uint16 address, Source source) const {
		return Append(RegisterViewMake<RegisterTable::Discrete, Source>(address, source));
	}


private:
	template<typename Entry>
	constexpr RegisterViewBuilder<Entry, Entries...> Append(Entry entry) const {
		return RegisterViewBuilder<Entry, Entries...>(RegisterViewList<Entry, Entries...>{ entry, list });
	}
};


// Writes the slots of a list, head first, from index on.
template<std::size_t Size>
constexpr void RegisterViewSlotsFill(const RegisterViewList<>&, std::array<RegisterViewSlot, Size>&, std::size_t) {
}


template<std::size_t Size, typename Head, typename... Tail>
constexpr void RegisterViewSlotsFill(const RegisterViewList<Head, Tail...>& list, std::array<RegisterViewSlot, Size>& slots, std::size_t index) {
	slots[index] = list.head.Slot();
	RegisterViewSlotsFill(list.tail, slots, index + 1);
}


template<std::size_t Size, typename List>
constexpr std::array<RegisterViewSlot, Size> RegisterViewSlotsOf(const List& list) {
	std::array<RegisterViewSlot, Size> slots{};
	RegisterViewSlotsFill(list, slots, 0);
	return slots;
}


// Calls the visitor with the entry at index, counting from the head. Returns badAddress when the index is past the list.
template<typename Visitor>
ResultStatus RegisterViewVisit(const RegisterViewList<>&, std::size_t, Visitor&) {
	return ResultStatus::badAddress;
}


template<typename Visitor, typename Head, typename... Tail>
ResultStatus RegisterViewVisit(const RegisterViewList<Head, Tail...>& list, std::size_t index, Visitor& visitor) {
	if (index == 0) {
		return visitor(list.head);
	}
	return RegisterViewVisit(list.tail, index - 1, visitor);
}


// The overlap check: two entries of one table may not share an address.
template<std::size_t Size>
constexpr bool RegisterViewCheck(const std::array<RegisterViewSlot, Size>& slots) {
	for (std::size_t first = 0; first < Size; first++) {
		for (std::size_t second = first + 1; second < Size; second++) {
			const RegisterViewSlot& left = slots[first];
			const RegisterViewSlot& right = slots[second];
			bool overlap = left.table == right.table && left.address < right.address + right.count && right.address < left.address + left.count;
			Require<"RegisterMap:two view entries overlap in the same table at one Modbus address">(!overlap);
		}
	}
	return true;
}


// True when some entry is an array, which a write may cover in part.
template<std::size_t Size>
constexpr bool RegisterViewHasPartial(const std::array<RegisterViewSlot, Size>& slots) {
	for (std::size_t index = 0; index < Size; index++) {
		if (slots[index].allowsPartial) {
			return true;
		}
	}
	return false;
}


// The view. Make is a captureless lambda that returns a RegisterViewBuilder. The entries are checked when the view is instantiated.
//
// Reads return ok, or badAddress when a register or coil of the range has no entry. A read may cover part of an entry.
// Writes check the whole request first and return badAddress (a range with no entry, a number or bool entry that is not covered
// whole, or a range that wraps past 0xFFFF), accessError (a read-only entry), or invalidData (a check rejected a value); nothing
// is written then. An array entry may be covered in part: its current value is read in the check pass and again in the write pass,
// so a partial write calls the source's get twice. A set function that fails during the write returns error, and the entries
// before it stay written.
template<auto (*Make)()>
struct RegisterView {
	using Built = decltype(Make());


	static constexpr Built built = Make();
	static constexpr std::size_t slotCount = Built::count;
	static constexpr std::array<RegisterViewSlot, slotCount> slots = RegisterViewSlotsOf<slotCount>(built.list);
	static constexpr bool checked = RegisterViewCheck(slots);
	static constexpr bool hasPartial = RegisterViewHasPartial(slots);


	static_assert(checked, "the view entries were not checked");


	static ResultStatus ReadWords(RegisterTable table, uint16 address, std::span<uint16> out) {
		std::size_t start = address;
		ResultStatus status = Cover(table, start, out.size(), false);
		if (status != ResultStatus::ok) {
			return status;
		}

		return Walk(table, start, out.size(), [&](const auto& entry, std::size_t first, std::size_t last) {
			return entry.ReadWords(first, out.subspan(first - start, last - first));
		});
	}


	static ResultStatus ReadBits(RegisterTable table, uint16 address, std::span<bool> out) {
		std::size_t start = address;
		ResultStatus status = Cover(table, start, out.size(), false);
		if (status != ResultStatus::ok) {
			return status;
		}

		return Walk(table, start, out.size(), [&](const auto& entry, std::size_t first, std::size_t last) {
			return entry.ReadBits(first, out.subspan(first - start, last - first));
		});
	}


	// Packs quantity coils or discrete inputs from address into out, least significant bit first, with the bytes that hold them
	// cleared first. out holds at least (quantity + 7) / 8 bytes. Each entry is read once, so each source runs once per call, however
	// many bytes the range crosses.
	static ResultStatus ReadPackedBits(RegisterTable table, uint16 address, std::size_t quantity, std::span<uint8> out) {
		std::size_t byteCount = (quantity + 7) / 8;
		if (out.size() < byteCount) {
			return ResultStatus::invalidParameter;
		}

		std::size_t start = address;
		ResultStatus status = Cover(table, start, quantity, false);
		if (status != ResultStatus::ok) {
			return status;
		}

		for (std::size_t index = 0; index < byteCount; index++) {
			out[index] = 0;
		}

		return Walk(table, start, quantity, [&](const auto& entry, std::size_t first, std::size_t last) {
			return entry.ReadPackedBits(first, last, start, out);
		});
	}


	static ResultStatus WriteWords(RegisterTable table, uint16 address, std::span<const uint16> values) {
		std::size_t start = address;
		ResultStatus status = Cover(table, start, values.size(), true);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = Walk(table, start, values.size(), [&](const auto& entry, std::size_t first, std::size_t last) {
			return entry.CheckWords(first, values.subspan(first - start, last - first));
		});
		if (status != ResultStatus::ok) {
			return status;
		}

		status = Walk(table, start, values.size(), [&](const auto& entry, std::size_t first, std::size_t last) {
			return entry.WriteWords(first, values.subspan(first - start, last - first));
		});
		return status == ResultStatus::ok ? ResultStatus::ok : ResultStatus::error;
	}


	static ResultStatus WriteBits(RegisterTable table, uint16 address, std::span<const bool> values) {
		std::size_t start = address;
		ResultStatus status = Cover(table, start, values.size(), true);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = Walk(table, start, values.size(), [&](const auto& entry, std::size_t first, std::size_t last) {
			return entry.CheckBits(first, values.subspan(first - start, last - first));
		});
		if (status != ResultStatus::ok) {
			return status;
		}

		status = Walk(table, start, values.size(), [&](const auto& entry, std::size_t first, std::size_t last) {
			return entry.WriteBits(first, values.subspan(first - start, last - first));
		});
		return status == ResultStatus::ok ? ResultStatus::ok : ResultStatus::error;
	}


private:
	// The index of the entry that holds position in the table, or slotCount when none does.
	static std::size_t FindSlot(RegisterTable table, std::size_t position) {
		for (std::size_t index = 0; index < slotCount; index++) {
			const RegisterViewSlot& slot = slots[index];
			if (slot.table == table && slot.address <= position && position < slot.address + slot.count) {
				return index;
			}
		}
		return slotCount;
	}


	// Checks that each address of the range has an entry. With whole set, each entry that does not allow a partial write must lie
	// inside the range. A view with no array entry compiles without the partial test.
	static ResultStatus Cover(RegisterTable table, std::size_t address, std::size_t count, bool whole) {
		std::size_t end = address + count;
		if (end > 0x10000) {
			return ResultStatus::badAddress;
		}

		std::size_t position = address;
		while (position < end) {
			std::size_t index = FindSlot(table, position);
			if (index == slotCount) {
				return ResultStatus::badAddress;
			}

			const RegisterViewSlot& slot = slots[index];
			if (whole && (slot.address < address || slot.address + slot.count > end)) {
				if constexpr (hasPartial) {
					if (!slot.allowsPartial) {
						return ResultStatus::badAddress;
					}
				} else {
					return ResultStatus::badAddress;
				}
			}
			position = slot.address + slot.count;
		}
		return ResultStatus::ok;
	}


	// Calls operation(entry, first, last) for each entry of the range, in address order. first and last are the part of the
	// range inside the entry.
	template<typename Operation>
	static ResultStatus Walk(RegisterTable table, std::size_t address, std::size_t count, Operation operation) {
		std::size_t end = address + count;
		std::size_t position = address;
		while (position < end) {
			std::size_t index = FindSlot(table, position);
			const RegisterViewSlot& slot = slots[index];
			std::size_t last = slot.address + slot.count < end ? slot.address + slot.count : end;

			ResultStatus status = Dispatch(index, [&](const auto& entry) {
				return operation(entry, position, last);
			});
			if (status != ResultStatus::ok) {
				return status;
			}
			position = last;
		}
		return ResultStatus::ok;
	}


	template<typename Visitor>
	static ResultStatus Dispatch(std::size_t index, Visitor visitor) {
		return RegisterViewVisit(built.list, index, visitor);
	}
};
