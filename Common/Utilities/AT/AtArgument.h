#pragma once
#include "AtError.h"
#include "AtValue.h"
#include <Utilities/CommandLine/CommandArgument.h>
#include <Utilities/CommandLine/CommandTypes.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


// The declared arguments of a command or a URC. A required argument is CommandLine's Argument<T> (CommandArgument.h), for an integer,
// bool or String<N>, so a console and an AT set in one program name their arguments the same way. An optional argument is
// Optional<T>("name"), for one that may be empty on the wire. The builder turns each Argument<T> into an AtArgument, so the set holds one
// type for every argument. A handler gets a string as a string_view, and an optional argument as std::optional.
//
// An integer argument has a range, from its type or from Range(low, high). The server refuses a value outside it, and the client drops a
// URC field outside it, in the same way as a value that does not fit its type. The client does not check the values it sends.


// The type a handler takes for a value type: string_view for a String, and the type itself otherwise.
template<typename T>
struct AtHandlerTypeOf {
	using Type = T;
};


template<std::size_t Capacity>
struct AtHandlerTypeOf<String<Capacity>> {
	using Type = std::string_view;
};


// The value a handler gets for a stored value: a view of a string, and the value itself otherwise. Both sides use it.
template<std::size_t Capacity>
std::string_view AtHandlerValue(const String<Capacity>& text) {
	return text.View();
}


template<typename T>
const T& AtHandlerValue(const T& value) {
	return value;
}


// True for a type whose arguments have a range: an integer, but not bool.
template<typename T>
inline constexpr bool AtIsRangedType = std::is_integral_v<T> && !std::is_same_v<T, bool>;


// The lowest and the highest value of an integer type, as the range of its argument. Other types have no range, so both are zero.
template<typename T>
constexpr int64 AtLowestOf() {
	if constexpr (AtIsRangedType<T>) {
		return static_cast<int64>(std::numeric_limits<T>::min());
	} else {
		return 0;
	}
}


template<typename T>
constexpr int64 AtHighestOf() {
	if constexpr (AtIsRangedType<T>) {
		return static_cast<int64>(std::numeric_limits<T>::max());
	} else {
		return 0;
	}
}


// A declared argument: its value type, whether it is optional, and its name, which the test text and the error messages use. An integer
// argument also has its range, minimum and maximum, which start at the limits of its type. Other types keep both at zero.
template<typename T, bool IsOptional>
struct AtArgument {
	using Type = T;
	using HandlerType = std::conditional_t<IsOptional, std::optional<typename AtHandlerTypeOf<T>::Type>, typename AtHandlerTypeOf<T>::Type>;
	static constexpr bool isOptional = IsOptional;
	std::string_view name{};
	int64 minimum = 0;
	int64 maximum = 0;


	// Narrows the range of an integer argument to low through high. Both bounds must fit the type, and low must not be above high. Other
	// types have no range, so the call is refused for them.
	template<typename Number>
	constexpr AtArgument Range(Number low, Number high) const {
		AtRequire<"a range is for an integer argument">(AtIsRangedType<T>);

		if constexpr (AtIsRangedType<T>) {
			AtRequire<"a range takes integers">(std::is_integral_v<Number>);
			AtRequire<"a range bound is outside the type of the argument">(CommandFitsType<T>(low) && CommandFitsType<T>(high));
			AtRequire<"a range has its minimum above its maximum">(low <= high);

			AtArgument copy = *this;
			copy.minimum = static_cast<int64>(low);
			copy.maximum = static_cast<int64>(high);
			return copy;
		} else {
			return *this;
		}
	}
};


// The range of one argument as the tables of a set keep it, for the runtime checks of the server and the client.
struct AtFieldRange {
	int64 minimum = 0;
	int64 maximum = 0;
};


// Checks a value type: a string needs a capacity above zero, and every type must be one the grammar carries.
template<typename T>
constexpr void AtCheckType() {
	AtRequire<"a string has a capacity of 0">(AtCapacityOf<T> > 0);
	AtRequire<"a type that the grammar does not support">(AtIsSupported<T>);
}


// The checks on a list of arguments: a required argument does not follow an optional one, and no two names are the same.
template<typename... Arguments>
constexpr void AtCheckArguments(const Arguments&... arguments) {
	std::array<bool, sizeof...(Arguments)> optional{Arguments::isOptional...};
	std::array<std::string_view, sizeof...(Arguments)> names{arguments.name...};

	bool seenOptional = false;

	for (bool isOptional : optional) {
		if (isOptional) {
			seenOptional = true;
		} else {
			AtRequire<"a required argument comes after an optional one">(!seenOptional);
		}
	}

	for (std::size_t index = 0; index < names.size(); index++) {
		AtRequire<"an argument has no name">(!names[index].empty());

		for (std::size_t other = 0; other < index; other++) {
			AtRequire<"two arguments of a command have the same name">(names[index] != names[other]);
		}
	}
}


// An optional argument of type T. It comes last in its list, and an empty field means absent. For an integer T, Range(low, high) narrows
// its range. A RawString is a reply value, so it is refused here.
template<typename T>
constexpr AtArgument<T, true> Optional(std::string_view name) {
	AtCheckType<T>();
	AtRequire<"RawString is a reply value, not an argument">(!AtIsRawString<T>);
	AtRequire<"an argument has no name">(!name.empty());
	return AtArgument<T, true>{name, AtLowestOf<T>(), AtHighestOf<T>()};
}


// An argument as the builder keeps it: an AtArgument is kept as it is. A CommandLine Argument<T> is a required AtArgument of its type, with
// the range that its declaration gives. Only integer, bool and string arguments are kept; a default makes an argument optional in
// CommandLine, so it is refused, and Verify is called as CommandLine calls it.
template<typename T, bool IsOptional>
constexpr AtArgument<T, IsOptional> AtNormalize(const AtArgument<T, IsOptional>& argument) {
	return argument;
}


template<typename Declaration>
constexpr int64 AtDeclaredMinimum(const Declaration& declaration) {
	if constexpr (AtIsRangedType<typename Declaration::Value>) {
		return static_cast<int64>(declaration.minimum);
	} else {
		static_cast<void>(declaration);
		return 0;
	}
}


template<typename Declaration>
constexpr int64 AtDeclaredMaximum(const Declaration& declaration) {
	if constexpr (AtIsRangedType<typename Declaration::Value>) {
		return static_cast<int64>(declaration.maximum);
	} else {
		static_cast<void>(declaration);
		return 0;
	}
}


template<typename Declaration>
constexpr AtArgument<typename Declaration::Value, false> AtNormalize(const Declaration& declaration) {
	using T = typename Declaration::Value;
	constexpr bool allowed = Declaration::Kind == CommandArgumentKind::Integer || Declaration::Kind == CommandArgumentKind::Boolean ||
		Declaration::Kind == CommandArgumentKind::Text;

	AtRequire<"an AT argument is an integer, bool or String<N>">(allowed);

	if constexpr (allowed) {
		AtRequire<"an AT argument has no default; declare it with Optional<T>">(!declaration.hasDefault);
		declaration.Verify();
		AtCheckType<T>();
		AtRequire<"an argument has no name">(!declaration.name.empty());
		return AtArgument<T, false>{declaration.name, AtDeclaredMinimum(declaration), AtDeclaredMaximum(declaration)};
	} else {
		return AtArgument<T, false>{};
	}
}


// The argument type a list of declarations gives: the AtArgument of each one.
template<typename Declaration>
using AtNormalizedOf = decltype(AtNormalize(std::declval<const Declaration&>()));


// True when one value can be the value of an argument: a required string takes text, an optional string takes text or no text, and any
// other type takes its own type, or an optional of it when the argument is optional.
template<typename Argument, typename Value>
constexpr bool AtAcceptsValue() {
	using T = typename Argument::Type;

	if constexpr (AtIsStringType<T>) {
		if constexpr (Argument::isOptional) {
			return std::is_convertible_v<Value, std::optional<std::string_view>>;
		} else {
			return std::is_convertible_v<Value, std::string_view>;
		}
	} else {
		if constexpr (Argument::isOptional) {
			return std::is_same_v<Value, T> || std::is_same_v<Value, std::optional<T>>;
		} else {
			return std::is_same_v<Value, T>;
		}
	}
}


template<typename Arguments, typename Tuple, std::size_t... K>
constexpr bool AtAcceptsAll(std::index_sequence<K...>) {
	return (AtAcceptsValue<std::tuple_element_t<K, Arguments>, std::tuple_element_t<K, Tuple>>() && ...);
}


// True when the values match the arguments one for one.
template<typename Arguments, typename... Values>
constexpr bool AtArgumentsAccept() {
	if constexpr (std::tuple_size_v<Arguments> != sizeof...(Values)) {
		return false;
	} else {
		return AtAcceptsAll<Arguments, std::tuple<Values...>>(std::make_index_sequence<sizeof...(Values)>{});
	}
}


// True when the values of a SendData call are the values of the arguments of its write form, and the last value is the data: a
// std::span<const uint8>. The data is not an argument, so it is checked apart from the others.
template<typename Arguments, typename... Values>
constexpr bool AtDataArgumentsAccept() {
	if constexpr (sizeof...(Values) == 0) {
		return false;
	} else {
		using Tuple = std::tuple<Values...>;
		constexpr std::size_t count = sizeof...(Values) - 1;

		if constexpr (std::tuple_size_v<Arguments> != count) {
			return false;
		} else {
			return std::is_same_v<std::tuple_element_t<count, Tuple>, std::span<const uint8>> &&
				AtAcceptsAll<Arguments, Tuple>(std::make_index_sequence<count>{});
		}
	}
}


// True when a value lies in the range of its argument. An integer is checked against its range. Other types have no range, so every value
// is in it.
template<typename T>
bool AtInRange(const T& value, const AtFieldRange& range) {
	if constexpr (AtIsRangedType<T>) {
		int64 number = static_cast<int64>(value);
		return number >= range.minimum && number <= range.maximum;
	} else {
		static_cast<void>(value);
		static_cast<void>(range);
		return true;
	}
}
