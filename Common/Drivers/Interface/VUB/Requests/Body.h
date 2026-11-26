#pragma once
#include <Utilities/CompileTime/Require.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>


namespace VUB {


static_assert(std::endian::native == std::endian::little, "VUB bodies are little-endian: the host must be little-endian");


// The layout of a body struct is given by a specialization of BodyOf, written once next to the struct:
//
//   template<>
//   struct BodyOf<Motor::MotorStart> {
//       static consteval auto Layout() {
//           return Fields<Motor::MotorStart>()
//               .Field<&Motor::MotorStart::speedRpm>()
//               .Field<&Motor::MotorStart::direction>()
//               .Optional<&Motor::MotorStart::rampMs>();
//       }
//   };
//
// The primary template has no Layout(), so a type with no specialization is not a body.
template<typename T>
struct BodyOf {
};


// True when T has a BodyOf specialization with a Layout().
template<typename T>
concept HasBodyOf = requires {
	BodyOf<T>::Layout();
};


// A body with no fields: the request of a command that takes none, and the reply of a command with a status only.
struct NoBody {
};


// How the codec reads one scalar element: an integer of width bytes (an enum, a float as its bit pattern, and the signed types are
// integers too), a bool that must be 0 or 1, or a nested body.
enum class FieldKind : uint8 {
	integer,
	boolean,
	nested
};


// One field of a body, as data. The codec reads and writes the fields of every body with the same functions, by walking a table of
// these. locate gives the address of the field in the object (the first element of an array).
struct FieldDescriptor {
	const void* (*locate)(const void* object) = nullptr;
	const struct BodyView* nested = nullptr;
	std::size_t count = 1;
	std::size_t stride = 0;
	FieldKind kind = FieldKind::integer;
	uint8 width = 0;
	bool optional = false;
};


// The layout of a whole body: its fields in wire order, and its sizes in bytes. minSize counts the required fields, maxSize all of them.
struct BodyView {
	const FieldDescriptor* fields = nullptr;
	std::size_t count = 0;
	std::size_t minSize = 0;
	std::size_t maxSize = 0;
	bool hasOptional = false;
};


// The wire bytes of one element of a field: a nested body is written with all its fields, so its size is its maximum.
constexpr std::size_t ElementWireSize(const FieldDescriptor& field) {
	if (field.kind == FieldKind::nested) {
		return field.nested->maxSize;
	}

	return field.width;
}


// The wire bytes of a whole field, every element of an array included.
constexpr std::size_t FieldWireSize(const FieldDescriptor& field) {
	return field.count * ElementWireSize(field);
}


// Reads the value of width little-endian bytes.
inline uint64 LoadLittleEndian(const uint8* bytes, std::size_t width) {
	uint64 value = 0;

	for (std::size_t index = 0; index < width; index++) {
		value |= static_cast<uint64>(bytes[index]) << (8 * index);
	}

	return value;
}


// Writes the low width bytes of value, little-endian.
inline void StoreLittleEndian(uint8* bytes, uint64 value, std::size_t width) {
	for (std::size_t index = 0; index < width; index++) {
		bytes[index] = static_cast<uint8>(value >> (8 * index));
	}
}


inline ResultStatus DecodeBody(const BodyView& view, const uint8* bytes, std::size_t size, void* object);


// Reads one element of a field into target. The element's bytes are at least ElementWireSize(field) long.
inline ResultStatus DecodeElement(const FieldDescriptor& field, const uint8* bytes, uint8* target) {
	if (field.kind == FieldKind::nested) {
		return DecodeBody(*field.nested, bytes, field.nested->maxSize, target);
	}

	if (field.kind == FieldKind::boolean) {
		if (bytes[0] > 1) {
			return ResultStatus::invalidData;
		}

		target[0] = bytes[0];
		return ResultStatus::ok;
	}

	const uint64 value = LoadLittleEndian(bytes, field.width);
	std::memcpy(target, &value, field.width);
	return ResultStatus::ok;
}


// Reads the fields of one body into object. The fields are read in order while bytes remain: an Optional field that finds no bytes
// keeps its value, a required field that finds too few bytes is invalidData, and bytes after the last field are ignored.
inline ResultStatus DecodeBody(const BodyView& view, const uint8* bytes, std::size_t size, void* object) {
	std::size_t offset = 0;

	for (std::size_t index = 0; index < view.count; index++) {
		const FieldDescriptor& field = view.fields[index];

		if (offset == size && field.optional) {
			return ResultStatus::ok;
		}

		if (size - offset < FieldWireSize(field)) {
			return ResultStatus::invalidData;
		}

		uint8* target = static_cast<uint8*>(const_cast<void*>(field.locate(object)));
		const std::size_t elementSize = ElementWireSize(field);

		for (std::size_t element = 0; element < field.count; element++) {
			const ResultStatus status = DecodeElement(field, bytes + offset + element * elementSize, target + element * field.stride);

			if (status != ResultStatus::ok) {
				return status;
			}
		}

		offset += FieldWireSize(field);
	}

	return ResultStatus::ok;
}


// Writes one element of a field. The element's bytes are at least ElementWireSize(field) long.
inline void EncodeBody(const BodyView& view, const void* object, uint8* bytes);


inline void EncodeElement(const FieldDescriptor& field, const uint8* source, uint8* bytes) {
	if (field.kind == FieldKind::nested) {
		EncodeBody(*field.nested, source, bytes);
		return;
	}

	uint64 value = 0;
	std::memcpy(&value, source, field.width);
	StoreLittleEndian(bytes, value, field.width);
}


// Writes every field of one body, optional fields included, in wire order. bytes holds at least view.maxSize bytes.
inline void EncodeBody(const BodyView& view, const void* object, uint8* bytes) {
	std::size_t offset = 0;

	for (std::size_t index = 0; index < view.count; index++) {
		const FieldDescriptor& field = view.fields[index];
		const uint8* source = static_cast<const uint8*>(field.locate(object));
		const std::size_t elementSize = ElementWireSize(field);

		for (std::size_t element = 0; element < field.count; element++) {
			EncodeElement(field, source + element * field.stride, bytes + offset + element * elementSize);
		}

		offset += FieldWireSize(field);
	}
}


// The traits of a data member pointer: its class and the type of the member.
template<typename Pointer>
struct MemberTraits;


template<typename ClassT, typename MemberT>
struct MemberTraits<MemberT ClassT::*> {
	using ClassType = ClassT;
	using MemberType = MemberT;
};


// A field is one element, or std::array<Element, Count> of them.
template<typename FieldT>
struct ElementTraits {
	using Element = FieldT;
	static constexpr std::size_t count = 1;
};


template<typename ElementT, std::size_t Length>
struct ElementTraits<std::array<ElementT, Length>> {
	using Element = ElementT;
	static constexpr std::size_t count = Length;
};


template<typename>
inline constexpr bool AlwaysFalse = false;


// A nested body's view, defined after the builder (the builder needs it for a nested field).
template<typename T>
constexpr const BodyView* NestedViewOf();


// True for the fixed-width integer types of Utilities/DataTypes.h. Where long or size_t is the same type as one of them on a target, it is
// accepted there too: the check is on the type, not on its name.
template<typename ElementT>
inline constexpr bool IsFixedWidthInteger = std::is_same_v<ElementT, int8> || std::is_same_v<ElementT, int16> || std::is_same_v<ElementT, int32> ||
	std::is_same_v<ElementT, int64> || std::is_same_v<ElementT, uint8> || std::is_same_v<ElementT, uint16> || std::is_same_v<ElementT, uint32> ||
	std::is_same_v<ElementT, uint64>;


// Fills in how one element of type ElementT is written. Each allowed type is named here, and the rest are compile errors. An integer field
// is one of the fixed-width types, so its wire width is the same on every target; an enum field has such an underlying type.
template<typename ElementT>
consteval void DescribeElement(FieldDescriptor& field) {
	if constexpr (std::is_same_v<ElementT, bool>) {
		field.kind = FieldKind::boolean;
		field.width = 1;
	} else if constexpr (IsFixedWidthInteger<ElementT>) {
		field.kind = FieldKind::integer;
		field.width = static_cast<uint8>(sizeof(ElementT));
	} else if constexpr (std::is_enum_v<ElementT>) {
		static_assert(IsFixedWidthInteger<std::underlying_type_t<ElementT>>,
			"an enum field has an underlying type that is not fixed width: give the enum an int8, int16, int32, int64, uint8, uint16, uint32 or uint64 underlying type");
		static_assert(sizeof(ElementT) <= 4, "an enum field is at most 4 bytes: this enum's underlying type is wider");
		field.kind = FieldKind::integer;
		field.width = static_cast<uint8>(sizeof(ElementT));
	} else if constexpr (std::is_integral_v<ElementT>) {
		static_assert(AlwaysFalse<ElementT>,
			"a body field has an integer type that is not fixed width: use int8, int16, int32, int64, uint8, uint16, uint32 or uint64 from Utilities/DataTypes.h (char, wchar_t, long, size_t and the others differ in width between targets)");
	} else if constexpr (std::is_same_v<ElementT, float>) {
		static_assert(std::numeric_limits<float>::is_iec559, "a float field needs IEEE 754 binary32 floats");
		field.kind = FieldKind::integer;
		field.width = 4;
	} else if constexpr (std::is_same_v<ElementT, double>) {
		static_assert(AlwaysFalse<ElementT>, "a double field is not in version 1: use a scaled integer");
	} else if constexpr (HasBodyOf<ElementT>) {
		static_assert(!NestedViewOf<ElementT>()->hasOptional, "a nested body has an Optional field: a nested body has only required fields in version 1");
		field.kind = FieldKind::nested;
		field.nested = NestedViewOf<ElementT>();
	} else {
		static_assert(AlwaysFalse<ElementT>, "a body field has a type that is not allowed: bool, an integer, an enum class of up to 4 bytes, float, a nested body with a BodyOf, or std::array of these");
	}
}


// Reads the address of a field of an object of class ClassType.
template<auto Member>
const void* LocateField(const void* object) {
	using ClassType = typename MemberTraits<decltype(Member)>::ClassType;
	return &(static_cast<const ClassType*>(object)->*Member);
}


// The descriptor of the member Member of a body T, as Optional or required.
template<typename T, auto Member, bool IsOptional>
consteval FieldDescriptor MakeField() {
	using ClassType = typename MemberTraits<decltype(Member)>::ClassType;
	using FieldType = typename MemberTraits<decltype(Member)>::MemberType;
	using ElementType = typename ElementTraits<FieldType>::Element;
	static_assert(std::is_same_v<ClassType, T>, "a field is not a member of the body type (an inherited member is not allowed)");

	FieldDescriptor field;
	field.locate = &LocateField<Member>;
	field.count = ElementTraits<FieldType>::count;
	field.stride = sizeof(ElementType);
	field.optional = IsOptional;
	DescribeElement<ElementType>(field);
	return field;
}


// The fields of a body T so far, in order. Field adds a required field, Optional an optional one. Optional fields come last.
template<typename T, std::size_t Count>
struct FieldsBuilder {
	std::array<FieldDescriptor, Count> fields{};
	bool hasOptional = false;


	template<auto Member>
	constexpr FieldsBuilder<T, Count + 1> Field() const {
		Require<"VUB:a required field follows an Optional field: Optional fields come last">(!hasOptional);
		return Append<Member, false>();
	}


	template<auto Member>
	constexpr FieldsBuilder<T, Count + 1> Optional() const {
		return Append<Member, true>();
	}


private:
	template<auto Member, bool IsOptional>
	constexpr FieldsBuilder<T, Count + 1> Append() const {
		FieldsBuilder<T, Count + 1> next{};

		for (std::size_t index = 0; index < Count; index++) {
			next.fields[index] = fields[index];
		}

		next.fields[Count] = MakeField<T, Member, IsOptional>();
		next.hasOptional = hasOptional || IsOptional;
		return next;
	}
};


// The start of a body's layout: no fields yet. Used by BodyOf<T>::Layout().
template<typename T>
consteval FieldsBuilder<T, 0> Fields() {
	return {};
}


// The view of a built layout. The builder must be a named object with static storage, so that the view's pointers are constants.
template<typename T, std::size_t Count>
consteval BodyView ViewOf(const FieldsBuilder<T, Count>& builder) {
	BodyView view;
	view.fields = builder.fields.data();
	view.count = Count;
	view.hasOptional = builder.hasOptional;

	for (std::size_t index = 0; index < Count; index++) {
		const FieldDescriptor& field = builder.fields[index];
		view.maxSize += FieldWireSize(field);

		if (!field.optional) {
			view.minSize += FieldWireSize(field);
		}
	}

	return view;
}


// The layout of body T as a static object, and the view of it. Every body has one of each.
template<typename T>
inline constexpr auto BodyLayoutOf = BodyOf<T>::Layout();


template<typename T>
inline constexpr BodyView BodyViewOf = ViewOf(BodyLayoutOf<T>);


template<typename T>
constexpr const BodyView* NestedViewOf() {
	return &BodyViewOf<T>;
}


template<>
struct BodyOf<NoBody> {
	static consteval auto Layout() {
		return Fields<NoBody>();
	}
};


// Writes body value into bytes: all of its fields, in order. Returns the number of bytes written, or messageTooLong when bytes is too
// short for the body's largest form.
template<typename T>
Result<std::size_t> Encode(const T& value, std::span<uint8> bytes) {
	static_assert(HasBodyOf<T>, "Encode needs a body type: give the type a BodyOf specialization with Layout()");
	const BodyView& view = BodyViewOf<T>;

	if (bytes.size() < view.maxSize) {
		return ResultStatus::messageTooLong;
	}

	EncodeBody(view, &value, bytes.data());
	return view.maxSize;
}


// Reads the body from bytes into value. A value of the body is reset to its defaults first, so an Optional field that the bytes do not
// reach keeps its default. Returns invalidData for a missing required field, a bool other than 0 or 1, or a field cut in the middle.
template<typename T>
ResultStatus Decode(std::span<const uint8> bytes, T& value) {
	static_assert(HasBodyOf<T>, "Decode needs a body type: give the type a BodyOf specialization with Layout()");
	value = T{};
	return DecodeBody(BodyViewOf<T>, bytes.data(), bytes.size(), &value);
}


}
