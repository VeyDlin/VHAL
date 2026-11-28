#pragma once
#include <array>
#include <cstddef>
#include <tuple>
#include <utility>


// Searches over a list of types, at compile time. A list is a std::tuple<...> or a TypeList::List<...>. A search gives the index of the
// first element for which a predicate holds, or the number of elements when none holds.
//
// A predicate is a type or an object with a member template Holds<Element>() that gives a bool. A predicate that holds a value, such as
// a key or a type to compare, is a class template with that value as its parameter, and its Holds reads the element's member. Holds is
// called for every element, so a search whose result is a constant is evaluated at compile time. The functions are constexpr, and they
// are compiled to code only where a call happens at run time.
namespace TypeList {


// A plain list of types, for a search over types that are not in a std::tuple.
template<typename... Types>
struct List {
};


// The std::tuple of a list: a std::tuple is its own tuple, and a List is the tuple of its types.
template<typename Container>
struct TupleOf {
	using type = Container;
};


template<typename... Types>
struct TupleOf<List<Types...>> {
	using type = std::tuple<Types...>;
};


template<typename Container>
using AsTuple = typename TupleOf<Container>::type;


// Declared, never defined. A call of it is a compile error, and the diagnostic names the predicate and the number of elements it holds
// for, which is 0 or more than 1. The count is a template argument, so the diagnostic shows it.
template<typename Predicate, std::size_t Value>
void ExactlyOnceFailed();


// The number of elements for which the predicate holds. The predicate's Holds is called for each element type of the tuple.
template<typename Tuple, typename Predicate, std::size_t... Index>
constexpr std::size_t CountWhereIn([[maybe_unused]] const Predicate& predicate, std::index_sequence<Index...>) {
	const std::array<bool, sizeof...(Index)> holds = {predicate.template Holds<std::tuple_element_t<Index, Tuple>>()...};
	std::size_t count = 0;
	for (std::size_t index = 0; index < holds.size(); index++) {
		if (holds[index]) {
			count++;
		}
	}

	return count;
}


// The index of the first element for which the predicate holds, or the number of elements when none holds.
template<typename Tuple, typename Predicate, std::size_t... Index>
constexpr std::size_t IndexWhereIn([[maybe_unused]] const Predicate& predicate, std::index_sequence<Index...>) {
	const std::array<bool, sizeof...(Index)> holds = {predicate.template Holds<std::tuple_element_t<Index, Tuple>>()...};
	for (std::size_t index = 0; index < holds.size(); index++) {
		if (holds[index]) {
			return index;
		}
	}

	return sizeof...(Index);
}


// The number of elements of the container for which the predicate holds.
template<typename Container, typename Predicate>
constexpr std::size_t CountWhere(const Predicate& predicate) {
	using Tuple = AsTuple<Container>;
	return CountWhereIn<Tuple>(predicate, std::make_index_sequence<std::tuple_size_v<Tuple>>());
}


// The index of the first element of the container for which the predicate holds, or the number of elements when none holds.
template<typename Container, typename Predicate>
constexpr std::size_t IndexWhere(const Predicate& predicate) {
	using Tuple = AsTuple<Container>;
	return IndexWhereIn<Tuple>(predicate, std::make_index_sequence<std::tuple_size_v<Tuple>>());
}


// The index of the only element of the container for which the predicate holds. The predicate is default-constructed, so it holds its
// value in its type. A predicate that holds for no element, or for more than one, is a compile error that names the predicate.
template<typename Container, typename Predicate>
constexpr std::size_t IndexWhereOnce() {
	constexpr std::size_t count = CountWhere<Container>(Predicate{});
	if constexpr (count != 1) {
		ExactlyOnceFailed<Predicate, count>();
	}

	return IndexWhere<Container>(Predicate{});
}


// The predicate that holds for the element whose type is exactly Type. A const or derived type is another type.
template<typename Type>
struct IsExactly {
	template<typename Element>
	static constexpr bool Holds() {
		return std::is_same_v<Element, Type>;
	}
};


// The index of the first element of the container whose type is exactly Type, or the number of elements when there is none.
template<typename Type, typename Container>
constexpr std::size_t IndexOf() {
	return IndexWhere<Container>(IsExactly<Type>{});
}


// The index of the only element of the container whose type is exactly Type. A type that is absent, or that is there twice, is a
// compile error that names the type.
template<typename Type, typename Container>
constexpr std::size_t IndexOfOnce() {
	return IndexWhereOnce<Container, IsExactly<Type>>();
}


// The index of the first item of an array for which the predicate holds, or the number of items when none holds. The predicate is called
// with a const reference to each item and gives a bool; a lambda or a function object serves. It searches a table of values, such as a
// table of commands looked up by id.
template<typename Item, std::size_t Count, typename Predicate>
constexpr std::size_t IndexWhereItem(const std::array<Item, Count>& items, const Predicate& predicate) {
	for (std::size_t index = 0; index < Count; index++) {
		if (predicate(items[index])) {
			return index;
		}
	}

	return Count;
}


}
