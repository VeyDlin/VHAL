#pragma once
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubLinkTraits.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubRequire.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>


// One block of a chain: its kind, its type and its options value. A builder keeps one entry per block, in the order the chain is
// written, from the application down to the link. The options value is given to the block's constructor when the block is built, so
// a block given options must have a constructor that takes them: the builder refuses a block that does not, rather than drop the
// options. A block given no options is default-constructed. The link takes no options: the link object is the user's and is built by
// the user.
template<VubKind Kind, typename Block, typename Options>
struct VubEntry {
	static constexpr VubKind kind = Kind;
	using BlockType = Block;
	using OptionsType = Options;
	// A link is not stored by the stack: the stack refers to the user's link object.
	using Stored = std::conditional_t<Kind == VubKind::link, VubNone, Block>;


	Options options{};


	// The packet size after this block, for the packet that enters the block with size bound. Layers add their header and transforms
	// their growth. Framing and the link add nothing: a packet is measured before framing.
	static constexpr std::size_t PacketAdd(std::size_t bound) {
		if constexpr (Kind == VubKind::layer) {
			return bound + Block::header;
		} else if constexpr (Kind == VubKind::transform) {
			return bound + Block::growth;
		} else {
			return bound;
		}
	}


	// The size after framing. Only framing adds to it, by its own growth for a packet of this size.
	static constexpr std::size_t FrameAdd(std::size_t bound) {
		if constexpr (Kind == VubKind::framing) {
			return bound + Block::Growth(bound);
		} else {
			return bound;
		}
	}


	static constexpr bool NeedsTime() {
		if constexpr (Kind == VubKind::layer || Kind == VubKind::framing) {
			return Block::usesTime;
		} else {
			return false;
		}
	}


	static constexpr bool MakesBulk() {
		if constexpr (Kind == VubKind::layer) {
			return Block::makesBulk;
		} else {
			return false;
		}
	}
};


// The settings of a chain that are not blocks. They are checked when the stack is instantiated.
struct VubSettings {
	std::size_t maxPayload = 0;
	std::size_t receiveRing = 256;
	std::size_t receiveQueue = 4;
	std::size_t sendQueue = 4;
	std::size_t perProcess = 8;
	uint32 (*clockFn)() = nullptr;
	void (*receiveFn)(const VubMeta&, std::span<const uint8>) = nullptr;
};


// The entry placed in front of an empty chain, so that the last-entry queries stay defined. It has no block.
struct VubNoEntry {
	static constexpr VubKind kind = VubKind::link;
	using BlockType = VubNone;
	using OptionsType = VubNone;
};


// The last entry of a list of entries.
template<typename... Entries>
struct VubLastEntry {
	using type = VubNoEntry;
};


template<typename Head, typename... Tail>
struct VubLastEntry<Head, Tail...> {
	using type = typename VubLastEntry<Tail...>::type;
};


template<typename Head>
struct VubLastEntry<Head> {
	using type = Head;
};


// The block type of the link entry, or VubNone when the list has none.
template<typename... Entries>
struct VubLinkOf {
	using type = VubNone;
};


template<typename Head, typename... Tail>
struct VubLinkOf<Head, Tail...> {
	using type = std::conditional_t<Head::kind == VubKind::link, typename Head::BlockType, typename VubLinkOf<Tail...>::type>;
};


// The traits of a link type, or the empty traits when the type is not a link.
template<typename Link>
constexpr VubLinkTraits VubTraitsOf() {
	if constexpr (requires { Link::traits; }) {
		return Link::traits;
	} else {
		return VubLinkTraits();
	}
}


// The entries of a chain as a recursive value list, in the style of RegisterViewList. A std::tuple inside a constant value that is made
// through a function pointer (the Make of Vub) does not compile with the current GCC, so the constant part of the builder uses this
// list, and only the run-time storage of the stack is a std::tuple.
template<typename... Entries>
struct VubList {
};


template<typename Head, typename... Tail>
struct VubList<Head, Tail...> {
	Head head;
	VubList<Tail...> tail;
};


// The list with entry added at the end.
template<typename Entry>
constexpr VubList<Entry> VubAppend(const VubList<>&, const Entry& entry) {
	return VubList<Entry>{entry, VubList<>{}};
}


template<typename Head, typename... Tail, typename Entry>
constexpr VubList<Head, Tail..., Entry> VubAppend(const VubList<Head, Tail...>& list, const Entry& entry) {
	return VubList<Head, Tail..., Entry>{list.head, VubAppend(list.tail, entry)};
}


// The type and access of entry I of a list.
template<std::size_t Index, typename List>
struct VubNth;


template<typename Head, typename... Tail>
struct VubNth<0, VubList<Head, Tail...>> {
	using type = Head;


	static constexpr Head& Of(VubList<Head, Tail...>& list) {
		return list.head;
	}


	static constexpr const Head& Of(const VubList<Head, Tail...>& list) {
		return list.head;
	}
};


template<std::size_t Index, typename Head, typename... Tail>
struct VubNth<Index, VubList<Head, Tail...>> {
	using type = typename VubNth<Index - 1, VubList<Tail...>>::type;


	static constexpr type& Of(VubList<Head, Tail...>& list) {
		return VubNth<Index - 1, VubList<Tail...>>::Of(list.tail);
	}


	static constexpr const type& Of(const VubList<Head, Tail...>& list) {
		return VubNth<Index - 1, VubList<Tail...>>::Of(list.tail);
	}
};


template<std::size_t Index, typename... Entries>
constexpr const typename VubNth<Index, VubList<Entries...>>::type& VubGet(const VubList<Entries...>& list) {
	return VubNth<Index, VubList<Entries...>>::Of(list);
}


template<std::size_t Index, typename... Entries>
constexpr typename VubNth<Index, VubList<Entries...>>::type& VubGet(VubList<Entries...>& list) {
	return VubNth<Index, VubList<Entries...>>::Of(list);
}


// A block that declares the member template Step has option methods that come right after it in a chain. The concept checks the name
// only; the Step is not instantiated here.
template<typename Block>
concept VubHasStep = requires {
	typename Block::template Step<VubNone>;
};


// The builder that follows a block. A block with a Step gives its Step over the next builder, so its option methods are callable right
// after it; a block without one gives the next builder.
template<typename Block, typename NextBuilder, bool HasStep = VubHasStep<Block>>
struct VubStepOf {
	using type = NextBuilder;
};


template<typename Block, typename NextBuilder>
struct VubStepOf<Block, NextBuilder, true> {
	using type = typename Block::template Step<NextBuilder>;
};


// The options a block is given when it is added without options. A block with a Step is given its own Options type, default-constructed,
// so that its option methods have a value to change. Any other block is given none.
template<typename Block, bool HasStep = VubHasStep<Block>>
struct VubDefaultOptionsOf {
	using type = VubNone;
};


template<typename Block>
struct VubDefaultOptionsOf<Block, true> {
	using type = typename Block::Options;
};


// The fluent builder of a chain. Each method that adds a block returns a builder whose type lists the blocks, so the chain is a type
// with a constexpr value of the options. The settings methods return the same type.
//
// Options of a block that come after it: the builder can read and replace the options of its last block with LastOptions and
// WithLastOptions. A sugar method for a built-in block checks the block with LastIs and then sets the options, so for example
// Retries(n) after Reliable() is a method on the builder that calls WithLastOptions. A user block gets the same by its type alone.
// A user block that declares a Step gets its option methods flat after it: the Step derives from the builder, so every builder method
// continues the chain, and the option methods exist only on the Step, so they are not callable after another block.
template<typename... Entries>
class VubBuilder {
public:
	using Tuple = std::tuple<Entries...>;
	using Storage = std::tuple<typename Entries::Stored...>;
	using LastEntry = typename VubLastEntry<Entries...>::type;
	using LinkType = typename VubLinkOf<Entries...>::type;
	static constexpr std::size_t count = sizeof...(Entries);


	// The builder a block method returns: the next builder with the entry added, or the block's Step over it.
	template<typename Block, typename Entry>
	using BuilderAfter = typename VubStepOf<Block, VubBuilder<Entries..., Entry>>::type;


	VubList<Entries...> entries;
	VubSettings settings;


	constexpr VubBuilder() = default;


	constexpr VubBuilder(VubList<Entries...> list, VubSettings values) : entries(list), settings(values) {
	}


	// The block methods take any type and check it first, so the message names the block's kind before the concept's notes.
	template<typename Block>
	constexpr VubBuilder<Entries..., VubEntry<VubKind::link, Block, VubNone>> Link() const {
		static_assert(VubLink<Block>, "the link does not satisfy VubLink: a link needs traits, and Write (a byte stream) or Send (packets)");
		return Append(VubEntry<VubKind::link, Block, VubNone>{});
	}


	template<typename Block>
	constexpr BuilderAfter<Block, VubEntry<VubKind::framing, Block, typename VubDefaultOptionsOf<Block>::type>> Framing() const {
		static_assert(VubFraming<Block>, "the framing does not satisfy VubFraming: it needs usesTime, Growth(length), Frame and Push");
		using Options = typename VubDefaultOptionsOf<Block>::type;
		return Extend<Block>(VubEntry<VubKind::framing, Block, Options>{});
	}


	template<typename Block, typename Options>
	constexpr BuilderAfter<Block, VubEntry<VubKind::framing, Block, Options>> Framing(const Options& options) const {
		static_assert(VubFraming<Block>, "the framing does not satisfy VubFraming: it needs usesTime, Growth(length), Frame and Push");
		return Extend<Block>(VubEntry<VubKind::framing, Block, Options>{options});
	}


	template<typename Block>
	constexpr BuilderAfter<Block, VubEntry<VubKind::transform, Block, typename VubDefaultOptionsOf<Block>::type>> Transform() const {
		static_assert(VubTransform<Block>, "the transform does not satisfy VubTransform: it needs growth, fingerprint, Encode and Decode");
		using Options = typename VubDefaultOptionsOf<Block>::type;
		return Extend<Block>(VubEntry<VubKind::transform, Block, Options>{});
	}


	template<typename Block, typename Options>
	constexpr BuilderAfter<Block, VubEntry<VubKind::transform, Block, Options>> Transform(const Options& options) const {
		static_assert(VubTransform<Block>, "the transform does not satisfy VubTransform: it needs growth, fingerprint, Encode and Decode");
		return Extend<Block>(VubEntry<VubKind::transform, Block, Options>{options});
	}


	template<typename Block>
	constexpr BuilderAfter<Block, VubEntry<VubKind::layer, Block, typename VubDefaultOptionsOf<Block>::type>> Layer() const {
		static_assert(VubLayer<Block>,
			"the layer does not satisfy VubLayer: it needs header, fingerprint, usesTime, makesBulk, Add, Take and Process");
		using Options = typename VubDefaultOptionsOf<Block>::type;
		return Extend<Block>(VubEntry<VubKind::layer, Block, Options>{});
	}


	template<typename Block, typename Options>
	constexpr BuilderAfter<Block, VubEntry<VubKind::layer, Block, Options>> Layer(const Options& options) const {
		static_assert(VubLayer<Block>,
			"the layer does not satisfy VubLayer: it needs header, fingerprint, usesTime, makesBulk, Add, Take and Process");
		return Extend<Block>(VubEntry<VubKind::layer, Block, Options>{options});
	}


	// The largest application payload, in bytes. Required.
	constexpr VubBuilder MaxPayload(std::size_t bytes) const {
		VubBuilder next = *this;
		next.settings.maxPayload = bytes;
		return next;
	}


	// The bytes a byte-stream link's receive ring holds.
	constexpr VubBuilder ReceiveRing(std::size_t bytes) const {
		VubBuilder next = *this;
		next.settings.receiveRing = bytes;
		return next;
	}


	// The packets a packet link's receive queue holds.
	constexpr VubBuilder ReceiveQueue(std::size_t packets) const {
		VubBuilder next = *this;
		next.settings.receiveQueue = packets;
		return next;
	}


	// The packets the send side holds while the link is busy. Send returns busy when they are full.
	constexpr VubBuilder SendQueue(std::size_t packets) const {
		VubBuilder next = *this;
		next.settings.sendQueue = packets;
		return next;
	}


	// The most packets one Process call sends. A bulk layer that always has data cannot hold the call past this.
	constexpr VubBuilder PerProcess(std::size_t packets) const {
		VubBuilder next = *this;
		next.settings.perProcess = packets;
		return next;
	}


	// The clock: a function that returns microseconds as uint32. Wrap-safe differences are taken by the blocks.
	template<uint32 (*Fn)()>
	constexpr VubBuilder Clock() const {
		VubBuilder next = *this;
		next.settings.clockFn = Fn;
		return next;
	}


	// The receive handler: called from Process with each packet that reached the top of the chain.
	template<void (*Fn)(const VubMeta&, std::span<const uint8>)>
	constexpr VubBuilder OnReceive() const {
		VubBuilder next = *this;
		next.settings.receiveFn = Fn;
		return next;
	}


	template<typename Block>
	static constexpr bool LastIs() {
		return std::is_same_v<typename LastEntry::BlockType, Block>;
	}


	constexpr typename LastEntry::OptionsType LastOptions() const {
		if constexpr (count > 0) {
			return VubGet<count - 1>(entries).options;
		} else {
			VubRequire<"LastOptions() needs a block before it">(false);
			return {};
		}
	}


	// Replaces the options of the last block. The options must have the type that block was given.
	template<typename Options>
	constexpr VubBuilder WithLastOptions(const Options& value) const {
		if constexpr (count > 0 && std::is_same_v<typename LastEntry::OptionsType, Options>) {
			VubBuilder next = *this;
			VubGet<count - 1>(next.entries).options = value;
			return next;
		} else {
			VubRequire<"WithLastOptions() needs a block before it, with options of the same type">(false);
			return *this;
		}
	}


	// The kind of each block, in chain order.
	static constexpr std::array<VubKind, count> Kinds() {
		return std::array<VubKind, count>{Entries::kind...};
	}


	// The index of the framing block, or count when there is none.
	static constexpr std::size_t FramingIndex() {
		const std::array<VubKind, count> kinds = Kinds();
		for (std::size_t index = 0; index < count; index++) {
			if (kinds[index] == VubKind::framing) {
				return index;
			}
		}

		return count;
	}


	// The number of layers and transforms: the blocks that change a packet on its way down.
	static constexpr std::size_t StepCount() {
		std::size_t total = 0;
		((total += (Entries::kind == VubKind::layer || Entries::kind == VubKind::transform) ? 1 : 0), ...);
		return total;
	}


	// The number of layers that make bulk packets.
	static constexpr std::size_t BulkCount() {
		std::size_t total = 0;
		((total += Entries::MakesBulk() ? 1 : 0), ...);
		return total;
	}


	static constexpr bool NeedsTime() {
		return (false || ... || Entries::NeedsTime());
	}


	static constexpr bool IsStreamLink() {
		return VubStreamLink<LinkType>;
	}


	static constexpr VubLinkTraits LinkTraits() {
		return VubTraitsOf<LinkType>();
	}


	// The largest packet the blocks build from a payload of settings.maxPayload bytes: the sum of the layer headers and transform growths.
	static constexpr std::size_t PacketBound(const VubSettings& values) {
		std::size_t bound = values.maxPayload;
		((bound = Entries::PacketAdd(bound)), ...);
		return bound;
	}


	// The largest framed packet on a byte-stream link: the packet bound with the framing's growth added.
	static constexpr std::size_t FramedBound(const VubSettings& values) {
		std::size_t bound = PacketBound(values);
		((bound = Entries::FrameAdd(bound)), ...);
		return bound;
	}


	// Builds the stored blocks from the entries, at run time. A block is moved into the tuple.
	static Storage MakeStorage(const VubList<Entries...>& values) {
		return MakeStorageAt(values, std::index_sequence_for<Entries...>{});
	}


	// Checks the chain. Every rule that is not met is a compile error with its message.
	static constexpr bool Check(const VubSettings& values) {
		(CheckOptions<Entries>(), ...);

		// The lengths of a packet and of a framed packet are 16-bit. The size rules come first: a constant expression stops at its first
		// failed check, so a size error is reported before a link error that follows from the same size.
		VubRequire<"a packet of 65536 bytes or more does not fit the 16-bit lengths: lower MaxPayload or the block headers and growths">(
			PacketBound(values) <= 65535);
		VubRequire<"a framed packet of 65536 bytes or more does not fit the 16-bit lengths: lower MaxPayload or the framing growth">(
			FramedBound(values) <= 65535);

		const std::array<VubKind, count> kinds = Kinds();
		std::size_t links = 0;
		std::size_t framings = 0;
		for (std::size_t index = 0; index < count; index++) {
			if (kinds[index] == VubKind::link) {
				links++;
			}

			if (kinds[index] == VubKind::framing) {
				framings++;
			}
		}

		VubRequire<"a chain needs a link: add Link<Type>() as the last block">(links > 0);
		VubRequire<"a chain has more than one link">(links <= 1);
		VubRequire<"a chain has two framings">(framings <= 1);
		if constexpr (count > 0) {
			VubRequire<"the link must be the last block of the chain">(kinds[count - 1] == VubKind::link);
		}

		constexpr VubLinkTraits traits = LinkTraits();
		if constexpr (VubPacketLink<LinkType>) {
			VubRequire<"a packet link takes no framing: remove the Framing<Type>() block">(framings == 0);
			VubRequire<"a packet link must be Framed(): its packets have boundaries of their own">(traits.framed);
		} else if constexpr (VubStreamLink<LinkType>) {
			VubRequire<"a stream link needs a framing: add Framing<Type>() right above the link">(framings == 1);
			VubRequire<"the framing must be right above the link">(framings == 0 || (count >= 2 && FramingIndex() == count - 2));
			VubRequire<"a stream link cannot be Framed(): its bytes have no packet boundaries">(!traits.framed);
		}

		if constexpr (VubLink<LinkType>) {
			VubRequire<"the link must say HalfDuplex() or FullDuplex()">(traits.duplex != VubDuplex::Unset);
			VubRequire<"a link has no MaxPacket: set MaxPacket(n) in its traits">(traits.maxPacket > 0);
			VubRequire<"the MaxPacket of the link is 65536 bytes or more: the received lengths are 16-bit">(traits.maxPacket <= 65535);
			VubRequire<"a packet is larger than the MaxPacket of the link: the payload and the headers and growths of the blocks add up to more">(
				PacketBound(values) <= traits.maxPacket);
		}

		// A ring or a queue of n slots needs n + 1 indexes of at most 65535, so it holds at most 65535 entries.
		VubRequire<"ReceiveRing() is zero">(values.receiveRing > 0);
		VubRequire<"the receive ring holds at most 65535 bytes: its indexes are 16-bit">(values.receiveRing <= 65535);
		VubRequire<"ReceiveQueue() is zero">(values.receiveQueue > 0);
		VubRequire<"the receive queue holds at most 65535 packets: its indexes are 16-bit">(values.receiveQueue <= 65535);
		VubRequire<"SendQueue() is zero">(values.sendQueue > 0);
		VubRequire<"the send queue holds at most 65535 packets: its indexes are 16-bit">(values.sendQueue <= 65535);
		VubRequire<"MaxPayload() is not given or is zero: set the largest application payload">(values.maxPayload > 0);
		VubRequire<"OnReceive<&Fn>() is missing: the receive handler takes each packet">(values.receiveFn != nullptr);
		VubRequire<"PerProcess() is zero">(values.perProcess > 0);
		VubRequire<"a block needs time and the chain has no clock: add Clock<&Fn>() to the builder">(!NeedsTime() || values.clockFn != nullptr);
		return true;
	}


private:
	template<std::size_t... Index>
	static Storage MakeStorageAt(const VubList<Entries...>& values, std::index_sequence<Index...>) {
		return Storage(MakeStored(VubGet<Index>(values))...);
	}


	// A block given options is built from them: Check has refused a block whose constructor does not take them. A block given no options
	// is default-constructed.
	template<typename Entry>
	static typename Entry::Stored MakeStored(const Entry& entry) {
		if constexpr (Entry::kind == VubKind::link) {
			return VubNone{};
		} else if constexpr (std::is_same_v<typename Entry::OptionsType, VubNone>) {
			return typename Entry::BlockType{};
		} else {
			return typename Entry::BlockType(entry.options);
		}
	}


	// A block given options must be built from them. The message names the block's kind; a link and a block with no options pass.
	template<typename Entry>
	static constexpr void CheckOptions() {
		if constexpr (Entry::kind == VubKind::layer && !std::is_same_v<typename Entry::OptionsType, VubNone>) {
			VubRequire<"the layer cannot be built from its options: give it a constructor that takes them">(
				std::is_constructible_v<typename Entry::BlockType, const typename Entry::OptionsType&>);
		} else if constexpr (Entry::kind == VubKind::transform && !std::is_same_v<typename Entry::OptionsType, VubNone>) {
			VubRequire<"the transform cannot be built from its options: give it a constructor that takes them">(
				std::is_constructible_v<typename Entry::BlockType, const typename Entry::OptionsType&>);
		} else if constexpr (Entry::kind == VubKind::framing && !std::is_same_v<typename Entry::OptionsType, VubNone>) {
			VubRequire<"the framing cannot be built from its options: give it a constructor that takes them">(
				std::is_constructible_v<typename Entry::BlockType, const typename Entry::OptionsType&>);
		}
	}


	template<typename Entry>
	constexpr VubBuilder<Entries..., Entry> Append(Entry entry) const {
		return VubBuilder<Entries..., Entry>(VubAppend(entries, entry), settings);
	}


	// Adds the entry of a block and returns the builder after it. A block with a Step gets the Step, so its option methods follow.
	template<typename Block, typename Entry>
	constexpr BuilderAfter<Block, Entry> Extend(const Entry& entry) const {
		using Next = VubBuilder<Entries..., Entry>;
		using AfterBlock = BuilderAfter<Block, Entry>;
		if constexpr (std::is_same_v<AfterBlock, Next>) {
			return Append(entry);
		} else {
			return AfterBlock(Append(entry));
		}
	}
};
