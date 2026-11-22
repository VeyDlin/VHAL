#pragma once
#include "AtArgument.h"
#include "AtDialect.h"
#include "AtError.h"
#include "AtForms.h"
#include "AtOptions.h"
#include "AtRow.h"
#include "Modules/AtDataMode.h"
#include <Utilities/CommandLine/CommandNameTree.h>
#include <Utilities/CommandLine/CommandPack.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>


// A description of an AT command set, built at compile time from a builder chain. One description gives the server and the client
// their tables, so the names, the forms, the argument types and the reply types are checked once, here.
//
//   using Sensor = AtCommandSet<+[] {
//       return AtCommandSetBuilder()
//           .Defaults(AtDefaults().TimeoutMs(2000))
//           .Modules(AtDataMode)
//           .Dialect(AtDialect().Echo().Verbose().Quiet())
//           .Command<"+TEMP">().Read<int16>("celsius")
//           .Command<"+LED">().Write(Argument<bool>("on"))
//           .Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password")).TimeoutMs(15000)
//           .Command<"+CWLAP">().ReadRows(AtRow<Network>(AtField<&Network::ssid>("ssid"))).Open("(").Close(")")
//           .Command<"+GMR">().Run<RawString<64>>("version").Reply("AT version:")
//           .Basic<"E">().Value(Argument<uint8>("on"))
//           .Urc<"WIFI GOT IP">();
//   }>;
//
// The chain is a captureless lambda, as for CommandSet and the FAT12 image. Each call returns a builder type that holds one more item, so the
// type of the chain carries every form and every argument. A command is open after its Command() call: its forms and options follow it, and
// the next Command(), Basic(), Urc() or set-level call closes it.
//
// Rules, checked at compile time (a violation is an error with its message, see AtError.h):
//   - a command name starts with + and has A-Z and 0-9 after it, with no space; a basic command is one letter A to Z;
//   - a name is declared once in a set, and a URC prefix once, and no URC prefix is a prefix of a command name or of a reply prefix;
//   - the dialect's builtin names (E for ATE, V, Q, S for ATS3 and ATS4, and +CMEE) are names of the set too: a basic E with Echo() is a
//     duplicate name;
//   - a URC prefix has no colon and no comma, since the prefix ends there;
//   - a form is declared once per command, so a command has at most one read form and one run form, and a basic command has only the run
//     and value forms; the value form is for a basic command;
//   - a write or value form has at least one argument, and no required argument follows an optional one;
//   - an argument is CommandLine's Argument<T> for an integer, bool or String<N>, or Optional<T>; an Argument with a default, or of
//     another kind (enum, float, custom), is refused, since the default belongs to Optional<T>;
//   - a row's fields are checked by AtRow (AtRow.h): a member of the row type, a grammar type, unique names, the optional fields last, and
//     a RawString last;
//   - a timeout is above zero, and a string has a capacity above zero;
//   - Defaults(), Modules() of one tag and Dialect() are given once; Modules() takes a module tag only;
//   - Prompt() is for a write form, given once, and a prompt command has a declared final; a prompt command and a Data() URC need
//     AtDataMode in the modules (AtDataMode.h);
//   - Data() is the last argument of a URC, after an unsigned integer length that is required.


// The check of a command name: a plus sign, then A-Z and 0-9, with no space.
template<AtText Name>
constexpr void AtCheckCommandName() {
	constexpr std::string_view text = Name.View();

	AtRequire<"a command name is empty">(!text.empty());

	if (!text.empty()) {
		AtRequire<"a command name does not start with +">(text[0] == '+');
		AtRequire<"a command name has a space">(text.find(' ') == std::string_view::npos);
		AtRequire<"a command name has no letters after +">(text.size() > 1);

		for (char character : text.substr(1)) {
			AtRequire<"a command name has a character that is not A-Z or 0-9">((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9'));
		}
	}
}


// The check of a basic command: one letter A to Z.
template<AtText Name>
constexpr void AtCheckBasicName() {
	constexpr std::string_view text = Name.View();
	AtRequire<"a basic command is not one letter A to Z">(text.size() == 1 && text[0] >= 'A' && text[0] <= 'Z');
}


// The check of a URC prefix: not empty, and no colon or comma, which end the prefix.
template<AtText Name>
constexpr void AtCheckUrcName() {
	constexpr std::string_view text = Name.View();

	AtRequire<"a URC prefix is empty">(!text.empty());
	AtRequire<"a URC prefix has a colon or a comma">(text.find(':') == std::string_view::npos && text.find(',') == std::string_view::npos);
}


// The commands of a list of items, in order, as a tuple of types.
template<typename Tuple>
struct AtCommandsOf;


template<>
struct AtCommandsOf<std::tuple<>> {
	using Type = std::tuple<>;
};


template<typename Head, typename Tuple>
struct AtPrepend;


template<typename Head, typename... Tail>
struct AtPrepend<Head, std::tuple<Tail...>> {
	using Type = std::tuple<Head, Tail...>;
};


template<typename Head, typename... Tail>
struct AtCommandsOf<std::tuple<Head, Tail...>> {
	using Rest = typename AtCommandsOf<std::tuple<Tail...>>::Type;
	using Type = std::conditional_t<AtIsCommandItem<Head>, typename AtPrepend<Head, Rest>::Type, Rest>;
};


// The URCs of a list of items, in order, as a tuple of types.
template<typename Tuple>
struct AtUrcsOf;


template<>
struct AtUrcsOf<std::tuple<>> {
	using Type = std::tuple<>;
};


template<typename Head, typename... Tail>
struct AtUrcsOf<std::tuple<Head, Tail...>> {
	using Rest = typename AtUrcsOf<std::tuple<Tail...>>::Type;
	using Type = std::conditional_t<AtIsUrcItem<Head>, typename AtPrepend<Head, Rest>::Type, Rest>;
};


// True when Item is one of the items of the tuple, or a dialect entry of one of its dialect items.
template<typename Item, typename Tuple>
inline constexpr bool AtHasItem = false;


template<typename Item, typename... Items>
inline constexpr bool AtHasItem<Item, std::tuple<Items...>> = ((std::is_same_v<Item, Items> || AtDialectHas<Item, Items>) || ...);


// The names of a tuple of commands or URCs, in order.
template<typename... Items>
constexpr std::array<std::string_view, sizeof...(Items)> AtNamesOf(std::tuple<Items...>) {
	return {Items::name...};
}


// The information of one command, which the server and the client read at run time.
struct AtCommandInfo {
	std::string_view name{};
	bool isBasic = false;
	bool isPrompt = false;
	std::size_t fieldCount = 0;
	std::size_t fieldBase = 0;
	std::array<bool, AtFormKindCount> hasForm{};
	AtCommandOptions options{};
};


// The table of commands, in declaration order, with each timeout resolved to the set default when the command gives none. Each command
// also gets the first of its fields in the set's numbering of fields, and the forms it declares, indexed by AtFormIndex.
template<std::size_t Count, typename Pack>
constexpr std::array<AtCommandInfo, Count> AtCommandInfosOf(const Pack& pack, uint32 defaultTimeoutMs) {
	std::array<AtCommandInfo, Count> infos{};
	std::size_t index = 0;
	std::size_t fieldTotal = 0;

	CommandPackForEach(pack, [&]<typename Item>(const Item& item) {
		using ItemType = std::remove_cvref_t<decltype(item)>;

		if constexpr (AtIsCommandItem<ItemType>) {
			AtCommandInfo info;
			info.name = ItemType::name;
			info.isBasic = ItemType::isBasic;
			info.isPrompt = item.prompt;
			info.fieldCount = ItemType::FieldCount;
			info.fieldBase = fieldTotal;
			fieldTotal += ItemType::FieldCount;
			info.hasForm[AtFormIndex(AtFormKind::test)] = ItemType::template CountOf<AtFormKind::test>() > 0;
			info.hasForm[AtFormIndex(AtFormKind::read)] = ItemType::template CountOf<AtFormKind::read>() > 0;
			info.hasForm[AtFormIndex(AtFormKind::write)] = ItemType::template CountOf<AtFormKind::write>() > 0;
			info.hasForm[AtFormIndex(AtFormKind::run)] = ItemType::template CountOf<AtFormKind::run>() > 0;
			info.hasForm[AtFormIndex(AtFormKind::value)] = ItemType::template CountOf<AtFormKind::value>() > 0;
			info.options = item.options;

			if (info.options.timeoutMs == 0) {
				info.options.timeoutMs = defaultTimeoutMs;
			}

			infos[index] = info;
			index++;
		}
	});

	return infos;
}


// The number of fields of all commands together: the size of the set's field numbering.
template<std::size_t Count>
constexpr std::size_t AtFieldTotalOf(const std::array<AtCommandInfo, Count>& infos) {
	std::size_t total = 0;

	for (const AtCommandInfo& info : infos) {
		total += info.fieldCount;
	}

	return total;
}


// The range of each argument that a command reads from a line, in the set's numbering of fields. A command's arguments are those of its
// write form or its value form, in declaration order, as AtCommandInfosOf numbers them.
template<std::size_t Total, typename Pack>
constexpr std::array<AtFieldRange, Total> AtFieldRangesOf(const Pack& pack) {
	std::array<AtFieldRange, Total> ranges{};
	std::size_t index = 0;

	CommandPackForEach(pack, [&]<typename Item>(const Item& item) {
		using ItemType = std::remove_cvref_t<decltype(item)>;

		if constexpr (AtIsCommandItem<ItemType>) {
			CommandPackForEach(item.forms, [&]<typename Form>(const Form& form) {
				using FormType = std::remove_cvref_t<decltype(form)>;

				if constexpr (AtIsArgumentForm<FormType>) {
					CommandPackForEach(form.arguments, [&]<typename Argument>(const Argument& argument) {
						ranges[index] = AtFieldRange{argument.minimum, argument.maximum};
						index++;
					});
				}
			});
		}
	});

	return ranges;
}


// The number of record and rows forms in a set: each one has a row table entry.
template<typename Pack>
constexpr std::size_t AtRowFormCountOf(const Pack& pack) {
	std::size_t count = 0;

	CommandPackForEach(pack, [&]<typename Item>(const Item& item) {
		using ItemType = std::remove_cvref_t<decltype(item)>;

		if constexpr (AtIsCommandItem<ItemType>) {
			CommandPackForEach(item.forms, [&]<typename Form>(const Form& form) {
				using FormType = std::remove_cvref_t<decltype(form)>;

				if constexpr (AtIsRowForm<FormType>) {
					count++;
				}
			});
		}
	});

	return count;
}


// The number of fields of all record and rows forms together: the size of the set's table of row field ranges.
template<typename Pack>
constexpr std::size_t AtReplyFieldTotalOf(const Pack& pack) {
	std::size_t total = 0;

	CommandPackForEach(pack, [&]<typename Item>(const Item& item) {
		using ItemType = std::remove_cvref_t<decltype(item)>;

		if constexpr (AtIsCommandItem<ItemType>) {
			CommandPackForEach(item.forms, [&]<typename Form>(const Form& form) {
				using FormType = std::remove_cvref_t<decltype(form)>;

				if constexpr (AtIsRowForm<FormType>) {
					total += FormType::Decl::FieldCount;
				}
			});
		}
	});

	return total;
}


// The row table of a set: the Open and Close texts of each record and rows form, with its command and its first field range.
template<std::size_t Count, typename Pack>
constexpr std::array<AtRowInfo, Count> AtRowInfosOf(const Pack& pack) {
	std::array<AtRowInfo, Count> infos{};
	std::size_t index = 0;
	std::size_t command = 0;
	std::size_t rangeBase = 0;

	CommandPackForEach(pack, [&]<typename Item>(const Item& item) {
		using ItemType = std::remove_cvref_t<decltype(item)>;

		if constexpr (AtIsCommandItem<ItemType>) {
			CommandPackForEach(item.forms, [&]<typename Form>(const Form& form) {
				using FormType = std::remove_cvref_t<decltype(form)>;

				if constexpr (AtIsRowForm<FormType>) {
					AtRowInfo info;
					info.command = command;
					info.isRun = FormType::kind == AtFormKind::run;
					info.open = form.decl.open;
					info.close = form.decl.close;
					info.rangeBase = rangeBase;
					infos[index] = info;
					index++;
					rangeBase += FormType::Decl::FieldCount;
				}
			});

			command++;
		}
	});

	return infos;
}


// The range of each field of each record and rows form, in the order of the row table.
template<std::size_t Total, typename Pack>
constexpr std::array<AtFieldRange, Total> AtReplyRangesOf(const Pack& pack) {
	std::array<AtFieldRange, Total> ranges{};
	std::size_t index = 0;

	CommandPackForEach(pack, [&]<typename Item>(const Item& item) {
		using ItemType = std::remove_cvref_t<decltype(item)>;

		if constexpr (AtIsCommandItem<ItemType>) {
			CommandPackForEach(item.forms, [&]<typename Form>(const Form& form) {
				using FormType = std::remove_cvref_t<decltype(form)>;

				if constexpr (AtIsRowForm<FormType>) {
					CommandPackForEach(form.decl.fields, [&]<typename Field>(const Field& field) {
						ranges[index] = AtFieldRange{field.minimum, field.maximum};
						index++;
					});
				}
			});
		}
	});

	return ranges;
}


// The first range slot of each URC's arguments, given the number of arguments of each URC. Each URC's arguments follow the one before it.
template<std::size_t Count>
constexpr std::array<std::size_t, Count> AtUrcBasesOf(const std::array<std::size_t, Count>& sizes) {
	std::array<std::size_t, Count> starts{};
	std::size_t next = 0;

	for (std::size_t index = 0; index < Count; index++) {
		starts[index] = next;
		next += sizes[index];
	}

	return starts;
}


// The number of URC arguments of all URCs together.
template<std::size_t Count>
constexpr std::size_t AtUrcTotalOf(const std::array<std::size_t, Count>& sizes) {
	std::size_t sum = 0;

	for (std::size_t size : sizes) {
		sum += size;
	}

	return sum;
}


// The layout of the URC arguments of a set: the number of arguments of each URC, the first slot of each, and the total.
template<typename Tuple>
struct AtUrcFieldLayout;


template<typename... Urcs>
struct AtUrcFieldLayout<std::tuple<Urcs...>> {
	static constexpr std::array<std::size_t, sizeof...(Urcs)> counts{std::tuple_size_v<typename Urcs::ArgumentTypes>...};
	static constexpr std::array<std::size_t, sizeof...(Urcs)> bases = AtUrcBasesOf(counts);
	static constexpr std::size_t total = AtUrcTotalOf(counts);
};


// The range of each URC argument, in the URC numbering of AtUrcFieldLayout. A URC has no form of its own, so each argument's range is the
// one its declaration gives.
template<std::size_t Total, typename Pack>
constexpr std::array<AtFieldRange, Total> AtUrcRangesOf(const Pack& pack) {
	std::array<AtFieldRange, Total> ranges{};
	std::size_t index = 0;

	CommandPackForEach(pack, [&]<typename Item>(const Item& item) {
		using ItemType = std::remove_cvref_t<decltype(item)>;

		if constexpr (AtIsUrcItem<ItemType>) {
			CommandPackForEach(item.arguments, [&]<typename Argument>(const Argument& argument) {
				ranges[index] = AtFieldRange{argument.minimum, argument.maximum};
				index++;
			});
		}
	});

	return ranges;
}


// The builtin commands that the dialect of the set declares, in dialect order.
template<typename ItemTypes>
constexpr AtBuiltinList AtBuiltinsOf() {
	AtBuiltinList list;

	if constexpr (AtHasItem<AtEchoItem, ItemTypes>) {
		list.kinds[list.count] = AtBuiltin::echo;
		list.count++;
	}

	if constexpr (AtHasItem<AtVerboseItem, ItemTypes>) {
		list.kinds[list.count] = AtBuiltin::verbose;
		list.count++;
	}

	if constexpr (AtHasItem<AtQuietItem, ItemTypes>) {
		list.kinds[list.count] = AtBuiltin::quiet;
		list.count++;
	}

	if constexpr (AtHasItem<AtRegistersItem, ItemTypes>) {
		list.kinds[list.count] = AtBuiltin::registers;
		list.count++;
	}

	if constexpr (AtHasItem<AtCmeCommandItem, ItemTypes>) {
		list.kinds[list.count] = AtBuiltin::cmee;
		list.count++;
	}

	return list;
}


// The names of the tree: the set's command names, then the builtin names of the dialect. A name's index in this list is its payload.
template<std::size_t Count, std::size_t BuiltinCount>
constexpr std::array<std::string_view, Count + BuiltinCount> AtTreeNamesOf(const std::array<std::string_view, Count>& names, const AtBuiltinList& list) {
	std::array<std::string_view, Count + BuiltinCount> all{};

	for (std::size_t index = 0; index < Count; index++) {
		all[index] = names[index];
	}

	for (std::size_t index = 0; index < BuiltinCount; index++) {
		all[Count + index] = AtBuiltinName(list.kinds[index]);
	}

	return all;
}


// The number of CmeText items in the dialect of a set.
template<typename Pack>
constexpr std::size_t AtCmeTextCountOf(const Pack& pack) {
	std::size_t count = 0;

	CommandPackForEach(pack, [&]<typename Entry>(const Entry& entry) {
		using EntryType = std::remove_cvref_t<decltype(entry)>;

		if constexpr (AtIsDialectItem<EntryType>) {
			CommandPackForEach(entry.items, [&]<typename Item>(const Item& item) {
				using ItemType = std::remove_cvref_t<decltype(item)>;

				if constexpr (AtIsCmeTextItem<ItemType>) {
					count++;
				}
			});
		}
	});

	return count;
}


// The CmeText items of the dialect of a set, as a table of exact size.
template<std::size_t Count, typename Pack>
constexpr std::array<AtCmeTextEntry, Count> AtCmeTextsOf(const Pack& pack) {
	std::array<AtCmeTextEntry, Count> table{};
	std::size_t index = 0;

	CommandPackForEach(pack, [&]<typename Entry>(const Entry& entry) {
		using EntryType = std::remove_cvref_t<decltype(entry)>;

		if constexpr (AtIsDialectItem<EntryType>) {
			CommandPackForEach(entry.items, [&]<typename Item>(const Item& item) {
				using ItemType = std::remove_cvref_t<decltype(item)>;

				if constexpr (AtIsCmeTextItem<ItemType>) {
					table[index] = AtCmeTextEntry{ItemType::code, ItemType::text};
					index++;
				}
			});
		}
	});

	return table;
}


// The default timeout of the set: the one AtDefaults gives, or AtFallbackTimeoutMs.
template<typename Pack>
constexpr uint32 AtDefaultTimeoutOf(const Pack& pack) {
	uint32 timeoutMs = 0;

	CommandPackForEach(pack, [&]<typename Entry>(const Entry& entry) {
		using EntryType = std::remove_cvref_t<decltype(entry)>;

		if constexpr (std::is_same_v<EntryType, AtDefaults>) {
			timeoutMs = entry.timeoutMs;
		}
	});

	return timeoutMs > 0 ? timeoutMs : AtFallbackTimeoutMs;
}


// The CME mode of the set's dialect, or off when it declares none.
template<typename Pack>
constexpr AtCmeMode AtCmeModeOf(const Pack& pack) {
	AtCmeMode mode = AtCmeMode::off;

	CommandPackForEach(pack, [&]<typename Entry>(const Entry& entry) {
		using EntryType = std::remove_cvref_t<decltype(entry)>;

		if constexpr (AtIsDialectItem<EntryType>) {
			mode = entry.CmeMode();
		}
	});

	return mode;
}


// The prefix tree of the set. Command names and their basic letters are in the root at node 0. URC prefixes are in a second root.
// Each node that ends a name holds the index of its command or URC as payload. The bound is the number of name characters plus the roots,
// which no tree exceeds, so the builder has room for every name.
template<std::size_t CommandCount, std::size_t UrcCount>
constexpr std::size_t AtTreeBound(const std::array<std::string_view, CommandCount>& commandNames, const std::array<std::string_view, UrcCount>& urcNames) {
	std::size_t bound = 2;

	for (std::string_view name : commandNames) {
		bound += name.size();
	}

	for (std::string_view name : urcNames) {
		bound += name.size();
	}

	return bound;
}


// True when a command's reply prefix does not end in a colon. Such a reply is followed by a comma, as in +CIFSR:STAIP,"...", and a set
// with none needs no separator code.
template<std::size_t Count>
constexpr bool AtHasCommaReplyOf(const std::array<AtCommandInfo, Count>& infos) {
	for (const AtCommandInfo& info : infos) {
		if (!info.options.replyPrefix.empty() && info.options.replyPrefix.back() != ':') {
			return true;
		}
	}

	return false;
}


// A letter or a digit: the characters of a word in a reply prefix.
constexpr bool AtWordCharacter(char character) {
	return static_cast<unsigned char>((character | 0x20) - 'a') < 26 || static_cast<unsigned char>(character - '0') < 10;
}


// The root of the command names in a set's tree, and the root of the URC prefixes. The builder adds the command root first.
constexpr uint16 AtCommandRoot = 0;
constexpr uint16 AtUrcRoot = 1;


// Builds the two roots and inserts every name. A duplicate name is a compile error.
template<std::size_t Bound, std::size_t CommandCount, std::size_t UrcCount>
constexpr CommandTreeBuilder<Bound> AtBuildTree(
	const std::array<std::string_view, CommandCount>& commandNames,
	const std::array<std::string_view, UrcCount>& urcNames)
{
	CommandTreeBuilder<Bound> builder;
	uint16 commandRoot = builder.AddRoot();
	uint16 urcRoot = builder.AddRoot();

	AtRequire<"the command and URC roots of the tree are not where the client expects them">(commandRoot == AtCommandRoot && urcRoot == AtUrcRoot);

	for (std::size_t index = 0; index < CommandCount; index++) {
		AtRequire<"a command name is declared twice in one set">(builder.Insert(commandRoot, commandNames[index], static_cast<uint16>(index)));
	}

	for (std::size_t index = 0; index < UrcCount; index++) {
		AtRequire<"a URC prefix is declared twice">(builder.Insert(urcRoot, urcNames[index], static_cast<uint16>(index)));
	}

	return builder;
}


// The used nodes of a builder, as an array of exact size.
template<std::size_t Count, std::size_t Bound>
constexpr std::array<CommandTreeNode, Count> AtCopyTree(const CommandTreeBuilder<Bound>& builder) {
	std::array<CommandTreeNode, Count> copy{};

	for (std::size_t index = 0; index < Count; index++) {
		copy[index] = builder.nodes[index];
	}

	return copy;
}


// True when a URC prefix clashes with the command names: a command name is a prefix of it (a line would be taken as the command's
// reply), or it is a prefix of a command name. Walks the command root one character at a time.
constexpr bool AtUrcClashesTree(std::span<const CommandTreeNode> nodes, std::string_view urc) {
	uint16 node = 0;

	for (char character : urc) {
		node = CommandTreeStep(nodes, node, character);

		if (node == CommandNoNode) {
			return false;
		}

		if (nodes[node].payload != CommandNoNode) {
			return true;
		}
	}

	return true;
}


// The final lines that every set has. A URC prefix must not be a prefix of one of them, or the URC would take the final line.
constexpr std::array<std::string_view, 3> AtStandardFinals{"OK", "ERROR", "+CME ERROR:"};


// The URC checks of a set: each prefix against the command names, against each explicit reply prefix, and against the final lines.
template<std::size_t TreeCount, std::size_t CommandCount, std::size_t UrcCount>
constexpr bool AtCheckUrcs(
	const std::array<CommandTreeNode, TreeCount>& tree,
	const std::array<AtCommandInfo, CommandCount>& commands,
	const std::array<std::string_view, UrcCount>& urcNames)
{
	for (std::string_view urc : urcNames) {
		AtRequire<"a URC prefix is a prefix of a command name, or the reverse">(!AtUrcClashesTree(std::span<const CommandTreeNode>(tree.data(), tree.size()), urc));

		for (std::string_view standard : AtStandardFinals) {
			AtRequire<"a URC prefix is a prefix of a final line, so the URC would take the final line">(!standard.starts_with(urc));
		}

		for (const AtCommandInfo& command : commands) {
			std::string_view reply = command.options.replyPrefix;

			if (!reply.empty()) {
				AtRequire<"a URC prefix is a prefix of a reply prefix, or the reverse">(!(urc.starts_with(reply) || reply.starts_with(urc)));
			}

			for (std::size_t index = 0; index < command.options.finalCount; index++) {
				AtRequire<"a URC prefix is a prefix of a final line, so the URC would take the final line">(!command.options.finals[index].starts_with(urc));
			}

			for (std::size_t index = 0; index < command.options.failureCount; index++) {
				AtRequire<"a URC prefix is a prefix of a final line, so the URC would take the final line">(!command.options.failures[index].starts_with(urc));
			}
		}
	}

	return true;
}


// A Data() argument is taken as it is: it has no value, so it is not normalized into an AtArgument.
constexpr AtDataDecl AtNormalize(const AtDataDecl& declaration) {
	return declaration;
}


// True when the last of a list of declarations is Data(). An empty list has no last declaration.
template<typename... Declarations>
struct AtLastIsDataOf {
	static constexpr bool value = false;
};


template<typename Head>
struct AtLastIsDataOf<Head> {
	static constexpr bool value = std::is_same_v<Head, AtDataDecl>;
};


template<typename Head, typename Second, typename... Tail>
struct AtLastIsDataOf<Head, Second, Tail...> : AtLastIsDataOf<Second, Tail...> {
};


// Data() may only be the last argument of a URC: the number of Data() arguments is zero, or one and it is the last.
template<typename... Declarations>
constexpr void AtCheckDataPosition() {
	constexpr std::size_t total = ((std::is_same_v<Declarations, AtDataDecl> ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
	constexpr std::size_t last = AtLastIsDataOf<Declarations...>::value ? std::size_t{1} : std::size_t{0};

	AtRequire<"Data() must be the last argument of a URC">(total == last);
}


// The last of a list of normalized arguments. An empty list gives an optional placeholder, which the length check refuses.
template<typename Tuple>
struct AtLastArgumentOf {
	using Type = AtArgument<uint8, true>;
};


template<typename Head, typename... Tail>
struct AtLastArgumentOf<std::tuple<Head, Tail...>> {
	using Type = std::conditional_t<(sizeof...(Tail) == 0), Head, typename AtLastArgumentOf<std::tuple<Tail...>>::Type>;
};


// True for the length of a Data() URC: a required argument of type uint8, uint16 or uint32.
template<typename Argument>
inline constexpr bool AtIsLengthArgument = false;


template<typename T, bool IsOptional>
inline constexpr bool AtIsLengthArgument<AtArgument<T, IsOptional>> =
	!IsOptional && (std::is_same_v<T, uint8> || std::is_same_v<T, uint16> || std::is_same_v<T, uint32>);


// Checks the arguments of a URC that are kept, which are all of them but a Data() last argument.
template<typename Normalized, std::size_t... K>
constexpr void AtCheckUrcArguments(const Normalized& normalized, std::index_sequence<K...>) {
	static_cast<void>(normalized);
	AtCheckArguments(std::get<K>(normalized)...);
}


// The URC item of the kept arguments: with Data, the length before it is the last kept argument and sets the type of the chunk offset.
template<AtText Name, bool HasData, typename Normalized, std::size_t... K>
constexpr auto AtUrcFrom(const Normalized& normalized, std::index_sequence<K...>) {
	static_cast<void>(normalized);
	AtCheckUrcArguments(normalized, std::index_sequence<K...>{});

	if constexpr (HasData) {
		using Kept = std::tuple<std::tuple_element_t<K, Normalized>...>;
		using Length = typename AtLastArgumentOf<Kept>::Type;

		AtRequire<"Data() needs a length argument before it">(sizeof...(K) > 0);
		AtRequire<"the length before Data() must be a required uint8, uint16 or uint32 argument">(AtIsLengthArgument<Length>);

		return AtUrcItem<Name, AtDataLength<typename Length::Type>, std::tuple_element_t<K, Normalized>...>{CommandPackOf(std::get<K>(normalized)...)};
	} else {
		return AtUrcItem<Name, AtNoData, std::tuple_element_t<K, Normalized>...>{CommandPackOf(std::get<K>(normalized)...)};
	}
}


// A URC of a list of arguments. A Data() last argument is taken out of the arguments and kept as the URC's Data.
template<AtText Name, typename... Arguments>
constexpr auto AtBuildUrc(const Arguments&... arguments) {
	AtCheckDataPosition<Arguments...>();

	constexpr bool hasData = AtLastIsDataOf<Arguments...>::value;
	constexpr std::size_t kept = sizeof...(Arguments) - (hasData ? std::size_t{1} : std::size_t{0});
	using NormalizedTuple = std::tuple<AtNormalizedOf<Arguments>...>;
	NormalizedTuple normalized = std::make_tuple(AtNormalize(arguments)...);

	return AtUrcFrom<Name, hasData>(normalized, std::make_index_sequence<kept>{});
}


// True when any URC of the tuple has Data().
template<typename... Urcs>
constexpr bool AtAnyUrcHasData(std::tuple<Urcs...>) {
	return (Urcs::hasData || ... || false);
}


// The data-mode rules of a set: a prompt command needs AtDataMode and a declared final (the line that ends it after its data), and so does a
// URC with Data().
template<std::size_t Count>
constexpr bool AtCheckDataModes(const std::array<AtCommandInfo, Count>& commands, bool anyUrcData, bool listed) {
	for (const AtCommandInfo& command : commands) {
		if (command.isPrompt) {
			AtRequire<"a prompt command needs AtDataMode among the modules of the set">(listed);
			AtRequire<"a prompt command needs a declared final, such as SEND OK, to end after its data">(command.options.finalCount > 0);
		}
	}

	if (anyUrcData) {
		AtRequire<"Data() in a URC needs AtDataMode among the modules of the set">(listed);
	}

	return true;
}


// The builder of a command set. Open is the command in progress, or AtNoCommand. Items are the closed items in declaration order: commands,
// URCs, defaults, modules and dialects. The items and the open command are values, so the chain is a constant expression.
template<typename Open, typename... Items>
class AtCommandSetBuilder {
public:
	using ItemTypes = std::tuple<Items...>;


	CommandPack<Items...> items{};
	Open open{};


	constexpr AtCommandSetBuilder() = default;


	constexpr AtCommandSetBuilder(CommandPack<Items...> itemList, Open current) : items(itemList), open(current) {
	}


	// A command. Its forms follow it, and its options follow the forms.
	template<AtText Name>
	constexpr auto Command() const {
		AtCheckCommandName<Name>();
		return OpenAfterClose<AtCommandItem<Name>>();
	}


	// A basic command: one letter, whose value follows it with no equals sign, as in ATE0.
	template<AtText Name>
	constexpr auto Basic() const {
		AtCheckBasicName<Name>();
		return OpenAfterClose<AtCommandItem<Name>>();
	}


	// A URC: the prefix and the arguments that follow it on the line. A Data() last argument takes the raw bytes after the length.
	template<AtText Name, typename... Arguments>
	constexpr auto Urc(Arguments... arguments) const {
		AtCheckUrcName<Name>();
		return AddItem(AtBuildUrc<Name>(arguments...));
	}


	// The set's defaults, such as AtDefaults().TimeoutMs(2000).
	constexpr auto Defaults(AtDefaults defaults) const {
		AtRequire<"Defaults() is given twice">(!Given<AtDefaults>());
		return AddItem(defaults);
	}


	// A module tag, such as AtDataMode. Each module is listed once, and a call takes one tag.
	template<typename Tag>
	constexpr auto Modules(Tag tag) const {
		AtRequire<"Modules() takes a module tag, such as AtDataMode">(AtIsModule<Tag>);
		AtRequire<"a module is given twice">(!Given<Tag>());
		return AddItem(tag);
	}


	// The dialect: the items of an AtDialect() chain. Given once.
	template<typename... Entries>
	constexpr auto Dialect(AtDialectBuilder<Entries...> dialect) const {
		AtRequire<"Dialect() is given twice">(!GivenDialect());
		return AddItem(AtDialectItem<Entries...>{dialect.items});
	}


	// Closes the open command. The result has no open command, and holds the command in its items.
	constexpr auto Close() const {
		if constexpr (std::is_same_v<Open, AtNoCommand>) {
			return AtCommandSetBuilder<AtNoCommand, Items...>(items, AtNoCommand{});
		} else {
			CommandPack<Items..., Open> closed = CommandPackAppend(items, open);
			return AtCommandSetBuilder<AtNoCommand, Items..., Open>(closed, AtNoCommand{});
		}
	}


	// AT+X=?
	constexpr AtCommandSetBuilder<typename Open::template With<AtTestForm>, Items...> Test() const {
		RequireForm<AtFormKind::test>();
		AtRequire<"a basic command has no test, read or write form">(!Open::isBasic);
		return AtCommandSetBuilder<typename Open::template With<AtTestForm>, Items...>(items, open.WithForm(AtTestForm{}));
	}


	// AT+X?, with one value of type T named name.
	template<typename T>
	constexpr AtCommandSetBuilder<typename Open::template With<AtReadForm<T>>, Items...> Read(std::string_view name) const {
		AtCheckType<T>();
		RequireForm<AtFormKind::read>();
		AtRequire<"a basic command has no test, read or write form">(!Open::isBasic);
		AtRequire<"a read form has no name">(!name.empty());
		return AtCommandSetBuilder<typename Open::template With<AtReadForm<T>>, Items...>(items, open.WithForm(AtReadForm<T>{name}));
	}


	// AT+X?, with one info line of the fields of a record. The fields are read into a value of the row type.
	template<typename RowDecl>
	requires AtIsRowDecl<RowDecl>
	constexpr AtCommandSetBuilder<typename Open::template With<AtReadRecordForm<RowDecl>>, Items...> Read(const RowDecl& row) const {
		RequireForm<AtFormKind::read>();
		AtRequire<"a basic command has no test, read or write form">(!Open::isBasic);
		return AtCommandSetBuilder<typename Open::template With<AtReadRecordForm<RowDecl>>, Items...>(
			items,
			open.WithForm(AtReadRecordForm<RowDecl>{row}));
	}


	// AT+X?, with one info line for each row. The client gives the rows to a handler, or visits them one at a time.
	template<typename RowDecl>
	requires AtIsRowDecl<RowDecl>
	constexpr AtCommandSetBuilder<typename Open::template With<AtReadRowsForm<RowDecl>>, Items...> ReadRows(const RowDecl& row) const {
		RequireForm<AtFormKind::read>();
		AtRequire<"a basic command has no test, read or write form">(!Open::isBasic);
		return AtCommandSetBuilder<typename Open::template With<AtReadRowsForm<RowDecl>>, Items...>(
			items,
			open.WithForm(AtReadRowsForm<RowDecl>{row}));
	}


	// AT+X=a,b. The arguments are CommandLine's Argument<T> objects or Optional<T> results, in any mix.
	template<typename... Arguments>
	constexpr AtCommandSetBuilder<typename Open::template With<AtWriteForm<AtNormalizedOf<Arguments>...>>, Items...> Write(Arguments... arguments) const {
		RequireForm<AtFormKind::write>();
		AtRequire<"a basic command has no test, read or write form">(!Open::isBasic);
		AtRequire<"a write form has no arguments">(sizeof...(Arguments) > 0);
		AtCheckArguments(AtNormalize(arguments)...);
		return AtCommandSetBuilder<typename Open::template With<AtWriteForm<AtNormalizedOf<Arguments>...>>, Items...>(
			items,
			open.WithForm(AtWriteForm<AtNormalizedOf<Arguments>...>{CommandPackOf(AtNormalize(arguments)...)}));
	}


	// AT+X
	constexpr AtCommandSetBuilder<typename Open::template With<AtRunForm>, Items...> Run() const {
		RequireForm<AtFormKind::run>();
		return AtCommandSetBuilder<typename Open::template With<AtRunForm>, Items...>(items, open.WithForm(AtRunForm{}));
	}


	// AT+X, with one value of type T named name in its info line, such as +GMR's text or +CIFSR's address.
	template<typename T>
	constexpr AtCommandSetBuilder<typename Open::template With<AtRunValueForm<T>>, Items...> Run(std::string_view name) const {
		AtCheckType<T>();
		RequireForm<AtFormKind::run>();
		AtRequire<"a run form has no name">(!name.empty());
		return AtCommandSetBuilder<typename Open::template With<AtRunValueForm<T>>, Items...>(items, open.WithForm(AtRunValueForm<T>{name}));
	}


	// AT+X, with one info line of the fields of a record, read into a value of the row type.
	template<typename RowDecl>
	requires AtIsRowDecl<RowDecl>
	constexpr AtCommandSetBuilder<typename Open::template With<AtRunRecordForm<RowDecl>>, Items...> Run(const RowDecl& row) const {
		RequireForm<AtFormKind::run>();
		return AtCommandSetBuilder<typename Open::template With<AtRunRecordForm<RowDecl>>, Items...>(
			items,
			open.WithForm(AtRunRecordForm<RowDecl>{row}));
	}


	// AT+X, with one info line for each row. The client gives the rows to a handler, or visits them one at a time.
	template<typename RowDecl>
	requires AtIsRowDecl<RowDecl>
	constexpr AtCommandSetBuilder<typename Open::template With<AtRunRowsForm<RowDecl>>, Items...> Rows(const RowDecl& row) const {
		RequireForm<AtFormKind::run>();
		return AtCommandSetBuilder<typename Open::template With<AtRunRowsForm<RowDecl>>, Items...>(
			items,
			open.WithForm(AtRunRowsForm<RowDecl>{row}));
	}


	// The write form of the open command is a prompt: the module answers with '>' after the request, and the client then writes the data of
	// the request (SendData). It needs AtDataMode in the set's modules, and a declared final that ends the request after the data.
	constexpr AtCommandSetBuilder<Open, Items...> Prompt() const {
		RequireOption();
		AtRequire<"Prompt() is for a write form, which comes before it">(Open::template CountOf<AtFormKind::write>() == 1);
		AtRequire<"Prompt() is given twice">(!open.prompt);
		return AtCommandSetBuilder<Open, Items...>(items, open.WithPrompt());
	}


	// The value of a basic command: ATE0 is Value(Argument<uint8>("on")) on the letter E.
	template<typename... Arguments>
	constexpr AtCommandSetBuilder<typename Open::template With<AtValueForm<AtNormalizedOf<Arguments>...>>, Items...> Value(Arguments... arguments) const {
		RequireForm<AtFormKind::value>();
		AtRequire<"a value form is for a basic command">(Open::isBasic);
		AtRequire<"a value form has no arguments">(sizeof...(Arguments) > 0);
		AtCheckArguments(AtNormalize(arguments)...);
		return AtCommandSetBuilder<typename Open::template With<AtValueForm<AtNormalizedOf<Arguments>...>>, Items...>(
			items,
			open.WithForm(AtValueForm<AtNormalizedOf<Arguments>...>{CommandPackOf(AtNormalize(arguments)...)}));
	}


	// The timeout of the open command, which overrides the set default.
	constexpr AtCommandSetBuilder<Open, Items...> TimeoutMs(uint32 milliseconds) const {
		RequireOption();
		AtRequire<"a timeout of 0">(milliseconds > 0);

		AtCommandOptions options = open.options;
		options.timeoutMs = milliseconds;
		return AtCommandSetBuilder<Open, Items...>(items, open.WithOptions(options));
	}


	// The info line prefix of the open command, for a reply that does not start with the command name, such as +CIFSR:STAIP.
	constexpr AtCommandSetBuilder<Open, Items...> Reply(std::string_view prefix) const {
		RequireOption();
		AtRequire<"a reply prefix is empty">(!prefix.empty());

		AtCommandOptions options = open.options;
		options.replyPrefix = prefix;
		return AtCommandSetBuilder<Open, Items...>(items, open.WithOptions(options));
	}


	// A final line that ends the reply as a success, such as SEND OK.
	constexpr AtCommandSetBuilder<Open, Items...> Finals(std::string_view text) const {
		RequireOption();
		AtRequire<"a final line is empty">(!text.empty());

		AtCommandOptions options = open.options;
		AtRequire<"a command has too many final lines">(options.finalCount < AtMaxFinals);
		options.finals[options.finalCount] = text;
		options.finalCount++;
		return AtCommandSetBuilder<Open, Items...>(items, open.WithOptions(options));
	}


	// A final line that ends the reply as a failure, such as FAIL or SEND FAIL.
	constexpr AtCommandSetBuilder<Open, Items...> Failures(std::string_view text) const {
		RequireOption();
		AtRequire<"a failure line is empty">(!text.empty());

		AtCommandOptions options = open.options;
		AtRequire<"a command has too many failure lines">(options.failureCount < AtMaxFinals);
		options.failures[options.failureCount] = text;
		options.failureCount++;
		return AtCommandSetBuilder<Open, Items...>(items, open.WithOptions(options));
	}


private:
	template<typename Entry>
	static constexpr bool Given() {
		return (std::is_same_v<Entry, Items> || ...);
	}


	static constexpr bool GivenDialect() {
		return (AtIsDialectItem<Items> || ...);
	}


	// The open command must be a command, and must not have a form of this kind yet.
	template<AtFormKind Kind>
	static constexpr void RequireForm() {
		AtRequire<"a form is declared with no command before it">(Open::isCommand);
		AtRequire<"a form is declared twice for one command">(Open::template CountOf<Kind>() == 0);
	}


	static constexpr void RequireOption() {
		AtRequire<"an option is declared with no command before it">(Open::isCommand);
	}


	// Closes the open command, if there is one, and opens a command of type NewOpen with no forms. The placeholder of "no command" is
	// never added to the items, so the items hold only the commands and the set-level items.
	template<typename NewOpen>
	constexpr auto OpenAfterClose() const {
		if constexpr (std::is_same_v<Open, AtNoCommand>) {
			return AtCommandSetBuilder<NewOpen, Items...>(items, NewOpen{});
		} else {
			CommandPack<Items..., Open> closed = CommandPackAppend(items, open);
			return AtCommandSetBuilder<NewOpen, Items..., Open>(closed, NewOpen{});
		}
	}


	// Closes the open command, if there is one, and adds item after it.
	template<typename Item>
	constexpr auto AddItem(const Item& item) const {
		if constexpr (std::is_same_v<Open, AtNoCommand>) {
			return AtCommandSetBuilder<AtNoCommand, Items..., Item>(CommandPackAppend(items, item), AtNoCommand{});
		} else {
			CommandPack<Items..., Open> closed = CommandPackAppend(items, open);
			return AtCommandSetBuilder<AtNoCommand, Items..., Open, Item>(CommandPackAppend(closed, item), AtNoCommand{});
		}
	}
};


// The chain starts with an empty builder: AtCommandSetBuilder().Command<"+X">()...
AtCommandSetBuilder() -> AtCommandSetBuilder<AtNoCommand>;


// The set of a chain. Make is the captureless lambda that returns the builder chain. The chain is evaluated here, so every rule of the
// builder is checked when the set is named.
template<auto (*Make)()>
class AtCommandSet {
public:
	using Built = decltype(Make().Close());
	using ItemTypes = typename Built::ItemTypes;
	using Commands = typename AtCommandsOf<ItemTypes>::Type;
	using Urcs = typename AtUrcsOf<ItemTypes>::Type;


	static constexpr Built built = Make().Close();

	static constexpr std::size_t CommandCount = std::tuple_size_v<Commands>;
	static constexpr std::size_t UrcCount = std::tuple_size_v<Urcs>;
	static constexpr uint32 DefaultTimeoutMs = AtDefaultTimeoutOf(built.items);
	static constexpr AtCmeMode CmeMode = AtCmeModeOf(built.items);

	static constexpr std::array<std::string_view, CommandCount> names = AtNamesOf(Commands{});
	static constexpr std::array<std::string_view, UrcCount> urcNames = AtNamesOf(Urcs{});
	static constexpr std::array<AtCommandInfo, CommandCount> commands = AtCommandInfosOf<CommandCount>(built.items, DefaultTimeoutMs);
	static constexpr std::size_t FieldTotal = AtFieldTotalOf(commands);
	static constexpr bool HasCommaReplyPrefix = AtHasCommaReplyOf(commands);

	// The range of each argument of a command, in the field numbering, and the first range of each URC with the total of the URC arguments.
	static constexpr std::array<AtFieldRange, FieldTotal> fieldRanges = AtFieldRangesOf<FieldTotal>(built.items);
	static constexpr std::array<std::size_t, UrcCount> urcFieldBases = AtUrcFieldLayout<Urcs>::bases;
	static constexpr std::array<AtFieldRange, AtUrcFieldLayout<Urcs>::total> urcFieldRanges =
		AtUrcRangesOf<AtUrcFieldLayout<Urcs>::total>(built.items);

	// The record and rows forms of the set: one row table entry for each, and the range of each of their fields.
	static constexpr std::size_t RowFormCount = AtRowFormCountOf(built.items);
	static constexpr std::array<AtRowInfo, RowFormCount> rowInfos = AtRowInfosOf<RowFormCount>(built.items);
	static constexpr std::size_t ReplyFieldTotal = AtReplyFieldTotalOf(built.items);
	static constexpr std::array<AtFieldRange, ReplyFieldTotal> replyRanges = AtReplyRangesOf<ReplyFieldTotal>(built.items);

	static constexpr std::size_t CmeTextCount = AtCmeTextCountOf(built.items);
	static constexpr std::array<AtCmeTextEntry, CmeTextCount> cmeTexts = AtCmeTextsOf<CmeTextCount>(built.items);

	// The builtin commands of the dialect. Each one is in the tree after the set's commands, so a builtin's payload is CommandCount plus its
	// index in builtinList.
	static constexpr AtBuiltinList builtinList = AtBuiltinsOf<ItemTypes>();
	static constexpr std::size_t BuiltinCount = builtinList.count;
	static constexpr std::array<std::string_view, CommandCount + BuiltinCount> treeNames = AtTreeNamesOf<CommandCount, BuiltinCount>(names, builtinList);

	static constexpr std::size_t TreeBound = AtTreeBound(treeNames, urcNames);
	static constexpr std::size_t TreeCount = AtBuildTree<TreeBound>(treeNames, urcNames).count;
	static constexpr std::array<CommandTreeNode, TreeCount> tree = AtCopyTree<TreeCount>(AtBuildTree<TreeBound>(treeNames, urcNames));
	static constexpr bool urcsChecked = AtCheckUrcs(tree, commands, urcNames);
	static constexpr bool dataChecked = AtCheckDataModes(commands, AtAnyUrcHasData(Urcs{}), AtHasItem<AtDataModeTag, ItemTypes>);

	// The chain and every table are evaluated here, so each rule of the builder is checked when the set is named. A type-only member
	// does not evaluate the chain, so this line is what makes the value checks run.
	static_assert(urcsChecked);
	static_assert(dataChecked);


	// The index of a command in the set's tables. A name the description does not declare is a compile error.
	template<AtText Name>
	static consteval std::size_t Index() {
		std::string_view text = Name.View();

		for (std::size_t index = 0; index < CommandCount; index++) {
			if (names[index] == text) {
				return index;
			}
		}

		AtError<"AT command name is not in the description">();
		return 0;
	}


	// The index of a name in the tree: a command, or a builtin of the dialect after the commands. A name the set does not declare is a
	// compile error.
	template<AtText Name>
	static consteval std::size_t TreeIndex() {
		std::string_view text = Name.View();

		for (std::size_t index = 0; index < treeNames.size(); index++) {
			if (treeNames[index] == text) {
				return index;
			}
		}

		AtError<"AT command name is not in the description">();
		return 0;
	}


	// The index of a URC in the set's URC names. A URC the description does not declare is a compile error.
	template<AtText Name>
	static consteval std::size_t UrcIndex() {
		std::string_view text = Name.View();

		for (std::size_t index = 0; index < UrcCount; index++) {
			if (urcNames[index] == text) {
				return index;
			}
		}

		AtError<"AT URC prefix is not in the description">();
		return 0;
	}


	// The index in rowInfos of the record or rows form of command C: the run form when Run is true, and the read form otherwise. It is
	// RowFormCount when the command has no such form.
	template<std::size_t C, bool Run>
	static constexpr std::size_t RowInfoIndex() {
		for (std::size_t index = 0; index < RowFormCount; index++) {
			if (rowInfos[index].command == C && rowInfos[index].isRun == Run) {
				return index;
			}
		}

		return RowFormCount;
	}


	// The type of the command at a position of the tables, with its forms and options.
	template<std::size_t Position>
	using CommandType = std::tuple_element_t<Position, Commands>;


	// The type of the URC at a position of the URC table, with its arguments.
	template<std::size_t Position>
	using UrcType = std::tuple_element_t<Position, Urcs>;


	// The type of the command with the given name.
	template<AtText Name>
	using CommandOf = CommandType<Index<Name>()>;


	// True when the set lists the item: a module tag, a dialect entry or a defaults item.
	template<typename Item>
	static constexpr bool Has = AtHasItem<Item, ItemTypes>;

	// True when the set lists AtDataMode: its prompt commands and its Data() URCs are then in use.
	static constexpr bool HasData = Has<AtDataModeTag>;
};
