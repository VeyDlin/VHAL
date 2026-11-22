#pragma once
#include "AtError.h"
#include <Utilities/CommandLine/CommandPack.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>


// The server dialect: the result-code and register options of AT. Each option is an item of AtDialect(), and a set that does not list
// an item has no code for it. The defaults, when an option is set, are ATE1, ATV1, ATQ0, S3=13, S4=10 and CMEE 0.


// How +CME ERROR is sent: CMEE 0 gives ERROR, 1 gives the numeric +CME ERROR: n, and 2 gives the text form.
enum class AtCmeMode : uint8 {
	off,
	numeric,
	verbose
};


// The option items. Each is a type, so a set holds only the items it declares.
struct AtEchoItem {};


struct AtVerboseItem {};


struct AtQuietItem {};


struct AtRegistersItem {};


struct AtCmeCommandItem {};


struct AtCmeErrorsItem {
	AtCmeMode mode = AtCmeMode::off;
};


// The verbose text of one CME code.
template<uint16 Code, AtText Text>
struct AtCmeTextItem {
	static constexpr uint16 code = Code;
	static constexpr std::string_view text = Text.View();
};


// The items of one AtDialect() chain, as a set item.
template<typename... Entries>
struct AtDialectItem {
	CommandPack<Entries...> items{};


	// The mode of the AtCmeErrorsItem in the chain, or off when there is none.
	constexpr AtCmeMode CmeMode() const {
		AtCmeMode mode = AtCmeMode::off;

		CommandPackForEach(items, [&]<typename Entry>(const Entry& entry) {
			using EntryType = std::remove_cvref_t<decltype(entry)>;

			if constexpr (std::is_same_v<EntryType, AtCmeErrorsItem>) {
				mode = entry.mode;
			}
		});

		return mode;
	}
};


template<typename Item>
inline constexpr bool AtIsDialectItem = false;


template<typename... Entries>
inline constexpr bool AtIsDialectItem<AtDialectItem<Entries...>> = true;


template<typename Item>
inline constexpr bool AtIsCmeTextItem = false;


template<uint16 Code, AtText Text>
inline constexpr bool AtIsCmeTextItem<AtCmeTextItem<Code, Text>> = true;


// The commands a dialect adds to the name tree, as a set declares them: ATE is E, ATV is V, ATQ is Q, ATS3 and ATS4 are S, and AT+CMEE.
// A basic command of the same letter, or a command of the same name, is a duplicate name in the set.
enum class AtBuiltin : uint8 {
	echo,
	verbose,
	quiet,
	registers,
	cmee
};


constexpr std::string_view AtBuiltinName(AtBuiltin kind) {
	switch (kind) {
		case AtBuiltin::echo:
			return "E";
		case AtBuiltin::verbose:
			return "V";
		case AtBuiltin::quiet:
			return "Q";
		case AtBuiltin::registers:
			return "S";
		case AtBuiltin::cmee:
			return "+CMEE";
	}

	return {};
}


// The builtin commands a set declares, in dialect order, and how many there are.
struct AtBuiltinList {
	std::array<AtBuiltin, 5> kinds{};
	std::size_t count = 0;
};


// The verbose text of one CME code, from CmeText.
struct AtCmeTextEntry {
	uint16 code = 0;
	std::string_view text{};
};


// True when the dialect item Pack holds the entry Item.
template<typename Item, typename Pack>
inline constexpr bool AtDialectHas = false;


template<typename Item, typename... Entries>
inline constexpr bool AtDialectHas<Item, AtDialectItem<Entries...>> = (std::is_same_v<Item, Entries> || ...);


// The builder of AtDialect(). Each call returns a builder with one more entry, and a repeated entry is a compile error.
template<typename... Entries>
class AtDialectBuilder {
public:
	CommandPack<Entries...> items{};


	constexpr AtDialectBuilder() = default;


	constexpr explicit AtDialectBuilder(CommandPack<Entries...> entryList) : items(entryList) {
	}


	// ATE: echo on or off.
	constexpr AtDialectBuilder<Entries..., AtEchoItem> Echo() const {
		return Add(AtEchoItem{});
	}


	// ATV: 1 for words, 0 for numeric result codes.
	constexpr AtDialectBuilder<Entries..., AtVerboseItem> Verbose() const {
		return Add(AtVerboseItem{});
	}


	// ATQ: 1 suppresses the result codes.
	constexpr AtDialectBuilder<Entries..., AtQuietItem> Quiet() const {
		return Add(AtQuietItem{});
	}


	// ATS3 and ATS4: the line terminator and the line feed characters, read and written at run time. S3 also ends an input line.
	constexpr AtDialectBuilder<Entries..., AtRegistersItem> Registers() const {
		return Add(AtRegistersItem{});
	}


	// The CME mode of the set. Alone, it is fixed, and AT+CMEE is not in the set. With CmeCommand() it is the mode at start.
	constexpr AtDialectBuilder<Entries..., AtCmeErrorsItem> CmeErrors(AtCmeMode mode) const {
		return Add(AtCmeErrorsItem{mode});
	}


	// AT+CMEE, which sets the CME mode at run time. Alone, the mode starts at 0. With CmeErrors() it starts at that mode.
	constexpr AtDialectBuilder<Entries..., AtCmeCommandItem> CmeCommand() const {
		return Add(AtCmeCommandItem{});
	}


	// The verbose text of one CME code, for a set that declares the code.
	template<uint16 Code, AtText Text>
	constexpr AtDialectBuilder<Entries..., AtCmeTextItem<Code, Text>> CmeText() const {
		return Add(AtCmeTextItem<Code, Text>{});
	}


private:
	template<typename Entry>
	static constexpr bool Given() {
		return (std::is_same_v<Entry, Entries> || ...);
	}


	template<typename Entry>
	constexpr AtDialectBuilder<Entries..., Entry> Add(const Entry& entry) const {
		AtRequire<"a dialect item is given twice">(!Given<Entry>());
		return AtDialectBuilder<Entries..., Entry>(CommandPackAppend(items, entry));
	}
};


// The start of a dialect chain: AtDialect().Echo().Verbose().Quiet().
constexpr AtDialectBuilder<> AtDialect() {
	return AtDialectBuilder<>{};
}
