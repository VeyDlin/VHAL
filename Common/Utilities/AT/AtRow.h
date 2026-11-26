#pragma once
#include "AtArgument.h"
#include "AtError.h"
#include "AtValue.h"
#include <Utilities/CommandLine/CommandArgument.h>
#include <Utilities/CommandLine/CommandPack.h>
#include <Utilities/Console/Print.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


// Records and rows. A record is one info line with fields, such as +JOIN: "ssid",... A row is one of the info lines of a list, such as
// +CWLAP:(4,"net",-72). Both are declared with their fields, which name a data member of the struct the handler gets:
//   struct Network {
//       int8 ecn;
//       String<32> ssid;
//       int8 rssi;
//   };
//   AtRow<Network>(AtField<&Network::ecn>("ecn"), AtField<&Network::ssid>("ssid"), AtField<&Network::rssi>("rssi").Range(-100, 0))
//       .Open("(")
//       .Close(")")
//
// A field's type is the member's type: an integer (with Range), bool, String<N>, RawString<N>, or std::optional of one of them for an
// optional field. An optional field comes last, and an empty field is absent. A RawString is the last field only: it takes the rest of the
// row, up to the Close text. The client reads the fields of a line that has more fields than declared, and skips the extra ones to the
// end of the row. A row with a required field missing is not a row. The client gives the row to its handler, and the server writes it.


// The class and the type of a data member pointer.
template<typename Member>
struct AtMemberOf;


template<typename Class, typename Value>
struct AtMemberOf<Value Class::*> {
	using Owner = Class;
	using Type = Value;
};


// The value type of a field, and the type inside an optional field.
template<typename Value>
struct AtFieldValueOf {
	using Base = Value;
	static constexpr bool isOptional = false;
};


template<typename Value>
struct AtFieldValueOf<std::optional<Value>> {
	using Base = Value;
	static constexpr bool isOptional = true;
};


// One field of a row: the data member, its name, and for an integer its range. The member is a template argument, so the field's type
// and its owner are known to the builder.
template<auto Member>
struct AtFieldDecl {
	using Owner = typename AtMemberOf<decltype(Member)>::Owner;
	using Value = typename AtMemberOf<decltype(Member)>::Type;
	using Base = typename AtFieldValueOf<Value>::Base;
	static constexpr decltype(Member) member = Member;
	static constexpr bool isOptional = AtFieldValueOf<Value>::isOptional;
	static constexpr bool isRaw = AtIsRawString<Base>;

	std::string_view name{};
	int64 minimum = 0;
	int64 maximum = 0;


	// Narrows the range of an integer field to low through high. Other types have no range, so the call is refused for them.
	template<typename Number>
	constexpr AtFieldDecl Range(Number low, Number high) const {
		Require<"AT:a range is for an integer field">(AtIsRangedType<Base>);

		if constexpr (AtIsRangedType<Base>) {
			Require<"AT:a range takes integers">(std::is_integral_v<Number>);
			Require<"AT:a range bound is outside the type of the field">(CommandFitsType<Base>(low) && CommandFitsType<Base>(high));
			Require<"AT:a range has its minimum above its maximum">(low <= high);

			AtFieldDecl copy = *this;
			copy.minimum = static_cast<int64>(low);
			copy.maximum = static_cast<int64>(high);
			return copy;
		} else {
			return *this;
		}
	}
};


// A field of a row, by its data member and its name: AtField<&Network::ssid>("ssid"). The member's type must be one the grammar carries.
template<auto Member>
constexpr AtFieldDecl<Member> AtField(std::string_view name) {
	using Field = AtFieldDecl<Member>;
	using Base = typename Field::Base;

	Require<"AT:a row field has no name">(!name.empty());
	Require<"AT:a row field has a type the grammar does not carry">(AtIsSupported<Base>);

	Field field{};
	field.name = name;
	field.minimum = AtLowestOf<Base>();
	field.maximum = AtHighestOf<Base>();
	return field;
}


// True for a row declaration of any row type and field list.
template<typename Decl>
inline constexpr bool AtIsRowDecl = false;


// The declaration of a record or a row: its row type, its fields in order, and the Open and Close texts. An empty text is absent.
template<typename RowT, typename... Fields>
struct AtRowDecl {
	using RowType = RowT;
	using FieldTypes = std::tuple<Fields...>;
	static constexpr std::size_t FieldCount = sizeof...(Fields);

	CommandPack<Fields...> fields{};
	std::string_view open{};
	std::string_view close{};


	// The text that a row starts with, after the reply prefix, such as "(".
	constexpr AtRowDecl Open(std::string_view text) const {
		Require<"AT:an Open text is empty">(!text.empty());

		AtRowDecl changed = *this;
		changed.open = text;
		return changed;
	}


	// The text that a row ends with, such as ")".
	constexpr AtRowDecl Close(std::string_view text) const {
		Require<"AT:a Close text is empty">(!text.empty());

		AtRowDecl changed = *this;
		changed.close = text;
		return changed;
	}
};


template<typename RowT, typename... Fields>
inline constexpr bool AtIsRowDecl<AtRowDecl<RowT, Fields...>> = true;


// The checks on the fields of a row: no two names are the same, a required field does not follow an optional one, and a RawString is the
// last field.
template<typename... Fields>
constexpr void AtCheckRowFields(const Fields&... fields) {
	std::array<std::string_view, sizeof...(Fields)> names{fields.name...};
	std::array<bool, sizeof...(Fields)> optional{Fields::isOptional...};
	std::array<bool, sizeof...(Fields)> raw{Fields::isRaw...};

	bool seenOptional = false;

	for (std::size_t index = 0; index < names.size(); index++) {
		if (optional[index]) {
			seenOptional = true;
		} else {
			Require<"AT:a required field comes after an optional one">(!seenOptional);
		}

		Require<"AT:RawString is the last field of a row">(!raw[index] || index + 1 == names.size());

		for (std::size_t other = 0; other < index; other++) {
			Require<"AT:two fields of a row have the same name">(names[index] != names[other]);
		}
	}
}


// A record or a row with its fields. A row type with no fields is refused, and every field must be a member of RowT.
template<typename RowT, typename... Fields>
constexpr AtRowDecl<RowT, Fields...> AtRow(Fields... fields) {
	Require<"AT:a row has no fields">(sizeof...(Fields) > 0);
	Require<"AT:a row field is a member of another struct">((std::is_same_v<typename Fields::Owner, RowT> && ...));
	AtCheckRowFields(fields...);
	return AtRowDecl<RowT, Fields...>{CommandPackOf(fields...)};
}


// The rows and records of a set, as the set's tables keep them: the command, whether it is the run form or the read form, the Open and
// Close texts, and the first range of its fields in the set's table of reply field ranges.
struct AtRowInfo {
	std::size_t command = 0;
	bool isRun = false;
	std::string_view open{};
	std::string_view close{};
	std::size_t rangeBase = 0;
};


// The characters of the fields of a row: each field's value and one separator, for the line buffer of the client.
template<typename Tuple>
struct AtRowCharsOf;


template<typename... Fields>
struct AtRowCharsOf<std::tuple<Fields...>> {
	static constexpr std::size_t value = ((AtValueChars<typename Fields::Base> + 1) + ... + std::size_t{0});
};


// ---- Client: reading a row --------------------------------------------------------------------------------------------------------------


// Splits the text of a row, from start, into its fields. A field ends at a comma or at the Close text outside quotes, or at the end of the
// text. The last declared field is raw when rawLast: it takes the text up to the Close text, quotes and all. Fields past Count are skipped.
// An empty field is left empty in parts. Returns false when the text ends in the middle of a raw field of a truncated line, since the raw
// text may be longer than its capacity. finish is the position where the row ends, at its Close text or at the end.
template<std::size_t Count>
bool AtSplitRow(
	std::string_view text,
	std::size_t start,
	std::string_view close,
	bool rawLast,
	bool overflow,
	std::array<std::string_view, Count>& parts,
	std::size_t& finish)
{
	std::size_t position = start;
	std::size_t field = 0;

	while (true) {
		bool raw = rawLast && field + 1 == Count;
		std::size_t end = position;

		if (raw) {
			std::size_t found = close.empty() ? std::string_view::npos : text.find(close, position);

			if (found == std::string_view::npos) {
				if (overflow) {
					return false;
				}

				end = text.size();
			} else {
				end = found;
			}
		} else {
			bool quoted = false;
			bool escaped = false;

			while (end < text.size()) {
				char character = text[end];

				if (quoted) {
					if (escaped) {
						escaped = false;
					} else if (character == '\\') {
						escaped = true;
					} else if (character == '"') {
						quoted = false;
					}
				} else if (character == '"') {
					quoted = true;
				} else if (character == ',') {
					break;
				} else if (!close.empty() && text.substr(end).starts_with(close)) {
					break;
				}

				end++;
			}
		}

		if (field < Count) {
			parts[field] = text.substr(position, end - position);
		}

		position = end;

		if (raw || position >= text.size() || text[position] != ',') {
			break;
		}

		position++;
		field++;
	}

	finish = position;
	return true;
}


// Reads one field: an empty field is absent when it is optional, and empty raw text when it is a RawString. Any other text is read as the
// field's type, and an integer must lie in the field's range.
template<typename Field, typename Row>
bool AtStoreRowField(std::string_view text, const AtFieldRange& range, Row& row) {
	using Base = typename Field::Base;

	if (text.empty()) {
		if constexpr (Field::isOptional) {
			row.*Field::member = std::nullopt;
			return true;
		} else if constexpr (Field::isRaw) {
			row.*Field::member = Base{};
			return true;
		} else {
			return false;
		}
	}

	AtValueReader<Base> reader{};

	for (char character : text) {
		reader.Push(character);
	}

	Base value{};

	if (reader.Finish(value) != ResultStatus::ok || !AtInRange(value, range)) {
		return false;
	}

	if constexpr (Field::isOptional) {
		row.*Field::member = std::optional<Base>(value);
	} else {
		row.*Field::member = value;
	}

	return true;
}


// Reads every field of a declaration from its text, in order. ranges is the first range of the row's fields.
template<typename Decl, std::size_t... K>
bool AtStoreRowFields(
	const std::array<std::string_view, sizeof...(K)>& parts,
	const AtFieldRange* ranges,
	typename Decl::RowType& row,
	std::index_sequence<K...>)
{
	return (AtStoreRowField<std::tuple_element_t<K, typename Decl::FieldTypes>>(parts[K], ranges[K], row) && ...);
}


// Reads the text of one info line into a row. The text starts after the reply prefix and its separator. The line must start with Open, when
// it is declared, and a row is complete at its Close text. An overflowing line (the buffer keeps only the first characters) is read as far
// as it goes, since every declared field lies inside the buffer. Returns false when the line is not a row of the declaration.
template<typename Decl>
bool AtReadRow(
	std::string_view text,
	bool overflow,
	std::string_view open,
	std::string_view close,
	const AtFieldRange* ranges,
	typename Decl::RowType& row)
{
	constexpr std::size_t count = Decl::FieldCount;
	constexpr bool rawLast = std::tuple_element_t<count - 1, typename Decl::FieldTypes>::isRaw;
	std::size_t start = 0;

	if (!open.empty()) {
		if (!text.starts_with(open)) {
			return false;
		}

		start = open.size();
	}

	std::array<std::string_view, count> parts{};
	std::size_t finish = start;

	if (!AtSplitRow<count>(text, start, close, rawLast, overflow, parts, finish)) {
		return false;
	}

	bool closed = close.empty() || text.substr(finish).starts_with(close);

	if (!closed && !overflow) {
		return false;
	}

	return AtStoreRowFields<Decl>(parts, ranges, row, std::make_index_sequence<count>{});
}


// ---- Server: writing a row --------------------------------------------------------------------------------------------------------------


// Writes a value of a field. An absent optional field writes nothing.
template<typename Sink, typename Value>
void AtWriteRowValue(Sink& out, const Value& value) {
	AtWriteValue(out, value);
}


template<typename Sink, typename Value>
void AtWriteRowValue(Sink& out, const std::optional<Value>& value) {
	if (value.has_value()) {
		AtWriteValue(out, *value);
	}
}


// True when a field has a value to write. A required field always has one.
template<typename Field, typename Row>
bool AtRowFieldPresent(const Row& row) {
	if constexpr (Field::isOptional) {
		return (row.*Field::member).has_value();
	} else {
		static_cast<void>(row);
		return true;
	}
}


// The number of fields to write: up to the last one with a value. Absent optional fields at the end are left out of the text.
template<typename Decl, std::size_t... K>
std::size_t AtLastWrittenField(const typename Decl::RowType& row, std::index_sequence<K...>) {
	std::size_t last = 0;

	((AtRowFieldPresent<std::tuple_element_t<K, typename Decl::FieldTypes>>(row) ? (last = K + 1) : last), ...);
	return last;
}


// Writes field K, and a comma before it when it is not the first. Fields from last on are not written.
template<typename Decl, std::size_t K>
void AtWriteRowField(Print& out, const typename Decl::RowType& row, std::size_t last) {
	using Field = std::tuple_element_t<K, typename Decl::FieldTypes>;

	if (K >= last) {
		return;
	}

	if constexpr (K > 0) {
		out.Write(std::string_view(","));
	}

	AtWriteRowValue(out, row.*Field::member);
}


template<typename Decl, std::size_t... K>
void AtWriteRowFields(Print& out, const typename Decl::RowType& row, std::size_t last, std::index_sequence<K...>) {
	(AtWriteRowField<Decl, K>(out, row, last), ...);
}


// Writes one row or record: its Open text, its fields and its Close text. The line itself is written by the caller.
template<typename Decl>
void AtWriteRow(Print& out, const typename Decl::RowType& row, std::string_view open, std::string_view close) {
	std::size_t last = AtLastWrittenField<Decl>(row, std::make_index_sequence<Decl::FieldCount>{});

	if (!open.empty()) {
		out.Write(open);
	}

	AtWriteRowFields<Decl>(out, row, last, std::make_index_sequence<Decl::FieldCount>{});

	if (!close.empty()) {
		out.Write(close);
	}
}


// Writes the prefix of an info line and the separator after it: a blank after a prefix that ends in a colon, and a comma after any other
// prefix, as in +CIFSR:STAIP,"...". Without a reply prefix the line starts with the command name and a colon.
inline void AtWriteInfoPrefix(Print& out, std::string_view name, std::string_view replyPrefix) {
	if (replyPrefix.empty()) {
		out.Write(name);
		out.Write(std::string_view(":"));
		out.Write(std::string_view(" "));
		return;
	}

	out.Write(replyPrefix);
	out.Write(std::string_view(replyPrefix.back() == ':' ? " " : ","));
}


// The writer of a rows reply on the server. Add writes one info line for a row: the line end, the prefix, the row and the line end. A
// handler gets one of these for its rows, and each Add writes at once.
template<typename Row>
class AtRowWriter {
public:
	using WriteFunction = void (*)(Print& out, const Row& row, std::string_view open, std::string_view close);


private:
	Print& out;
	char lineEndChar = '\r';
	char lineFeedChar = '\n';
	std::string_view name{};
	std::string_view replyPrefix{};
	std::string_view open{};
	std::string_view close{};
	WriteFunction writeRow = nullptr;


public:
	AtRowWriter(
		Print& output,
		char lineEnd,
		char lineFeed,
		std::string_view commandName,
		std::string_view prefix,
		std::string_view openText,
		std::string_view closeText,
		WriteFunction write)
		: out(output),
		  lineEndChar(lineEnd),
		  lineFeedChar(lineFeed),
		  name(commandName),
		  replyPrefix(prefix),
		  open(openText),
		  close(closeText),
		  writeRow(write)
	{
	}


	// Writes one row as one info line.
	void Add(const Row& row) {
		WriteLineEnd();
		AtWriteInfoPrefix(out, name, replyPrefix);
		writeRow(out, row, open, close);
		WriteLineEnd();
	}


private:
	void WriteLineEnd() {
		char pair[2] = {lineEndChar, lineFeedChar};
		out.Write(std::string_view(pair, 2));
	}
};
