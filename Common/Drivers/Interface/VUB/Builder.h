#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace VUB {


// One block of a chain: its kind, its type and its options value. A builder keeps one entry per block, in the order the chain is
// written, from the application down to the link. The options value is given to the block's constructor when the block is built, so
// a block given options must have a constructor that takes them: the builder refuses a block that does not, rather than drop the
// options. A block given no options is default-constructed. The link takes no options: the link object is the user's and is built by
// the user.
template<BlockKind Kind, typename Block, typename Options>
struct BlockEntry {
	static constexpr BlockKind kind = Kind;
	using BlockType = Block;
	using OptionsType = Options;
	// A link is not stored by the stack: the stack refers to the user's link object.
	using Stored = std::conditional_t<Kind == BlockKind::link, None, Block>;


	Options options{};


	// The packet size after this block, for the packet that enters the block with size bound. Layers add their header and transforms
	// their growth. Framing and the link add nothing: a packet is measured before framing.
	static constexpr std::size_t PacketAdd(std::size_t bound) {
		if constexpr (Kind == BlockKind::layer) {
			return bound + Block::header;
		} else if constexpr (Kind == BlockKind::transform) {
			return bound + Block::growth;
		} else {
			return bound;
		}
	}


	// The size after framing. Only framing adds to it, by its own growth for a packet of this size.
	static constexpr std::size_t FrameAdd(std::size_t bound) {
		if constexpr (Kind == BlockKind::framing) {
			return bound + Block::Growth(bound);
		} else {
			return bound;
		}
	}


	static constexpr bool NeedsTime() {
		if constexpr (Kind == BlockKind::layer || Kind == BlockKind::framing) {
			return Block::usesTime;
		} else {
			return false;
		}
	}


	static constexpr bool MakesBulk() {
		if constexpr (Kind == BlockKind::layer) {
			return Block::makesBulk;
		} else {
			return false;
		}
	}


	// Whether the block is a layer that declares its peer (reportsPeer = true): it can take the connection down from Process.
	static constexpr bool ReportsPeer() {
		if constexpr (Kind == BlockKind::layer) {
			if constexpr (requires { Block::reportsPeer; }) {
				return Block::reportsPeer;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}


	// Whether the block is a layer that asks the physical link's state from its Process (usesLinkState = true): it may call outbox.LinkUp.
	static constexpr bool UsesLinkState() {
		if constexpr (Kind == BlockKind::layer) {
			if constexpr (requires { Block::usesLinkState; }) {
				return Block::usesLinkState;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}


	// Whether the block is a layer that passes packets up from its Process (passesUp = true): it may call outbox.Deliver.
	static constexpr bool PassesUp() {
		if constexpr (Kind == BlockKind::layer) {
			if constexpr (requires { Block::passesUp; }) {
				return Block::passesUp;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}
};


// The default value of an options type, for a template argument that asks whether a block declares Configured.
template<typename Options>
inline constexpr Options DefaultValueOf{};


// Whether a block declares Configured for the options value: the stack then stores it as Configured<value>, with the options as constants.
// A block that does not declare it is built from its options at run time.
template<typename Entry, auto Value>
concept ConfiguredEntry = requires {
	typename Entry::BlockType::template Configured<Value>;
};


// Whether a block declares Configured for some options: the builder then does not need a constructor that takes them.
template<typename Entry>
concept DeclaresConfigured = ConfiguredEntry<Entry, DefaultValueOf<typename Entry::OptionsType>>;


// The type a stack stores for an entry, given the value of its options: the block's Configured alias with those options, or the entry's
// Stored type when the block does not declare Configured.
template<typename Entry, auto Value, bool IsConfigured = ConfiguredEntry<Entry, Value>>
struct StoredOf {
	using type = typename Entry::Stored;
};


template<typename Entry, auto Value>
struct StoredOf<Entry, Value, true> {
	using type = typename Entry::BlockType::template Configured<Value>;
};


// The position of the first block of type Block in the entries of a chain, or the number of entries when there is none.
template<typename Tuple, typename Block, std::size_t Index = 0>
constexpr std::size_t BlockIndexOf() {
	if constexpr (Index >= std::tuple_size_v<Tuple>) {
		return Index;
	} else if constexpr (std::is_same_v<typename std::tuple_element_t<Index, Tuple>::BlockType, Block>) {
		return Index;
	} else {
		return BlockIndexOf<Tuple, Block, Index + 1>();
	}
}


// The element of a storage at index, or None when the index is past the storage: Find reports a block that is not in the chain.
template<std::size_t Index, typename Storage>
using StoredElement = std::conditional_t<(Index < std::tuple_size_v<Storage>),
	std::tuple_element_t<(Index < std::tuple_size_v<Storage> ? Index : 0), Storage>, None>;


// The settings of a chain that are not blocks. They are checked when the stack is instantiated.
struct Settings {
	std::size_t maxPayload = 0;
	std::size_t receiveRing = 256;
	std::size_t receiveQueue = 4;
	std::size_t sendQueue = 4;
	std::size_t perProcess = 8;
	uint32 (*clockFn)() = nullptr;
	void (*receiveFn)(const Meta&, std::span<const uint8>) = nullptr;
	Down whileDown = Down::Wait;
};


// The entry placed in front of an empty chain, so that the last-entry queries stay defined. It has no block.
struct NoBlockEntry {
	static constexpr BlockKind kind = BlockKind::link;
	using BlockType = None;
	using OptionsType = None;
};


// The last entry of a list of entries.
template<typename... Entries>
struct LastBlockEntry {
	using type = NoBlockEntry;
};


template<typename Head, typename... Tail>
struct LastBlockEntry<Head, Tail...> {
	using type = typename LastBlockEntry<Tail...>::type;
};


template<typename Head>
struct LastBlockEntry<Head> {
	using type = Head;
};


// The block type of the link entry, or None when the list has none.
template<typename... Entries>
struct LinkOf {
	using type = None;
};


template<typename Head, typename... Tail>
struct LinkOf<Head, Tail...> {
	using type = std::conditional_t<Head::kind == BlockKind::link, typename Head::BlockType, typename LinkOf<Tail...>::type>;
};


// The traits of a link type, or the empty traits when the type is not a link.
template<typename Link>
constexpr LinkTraits TraitsOf() {
	if constexpr (requires { Link::traits; }) {
		return Link::traits;
	} else {
		return LinkTraits();
	}
}


// The entries of a chain as a recursive value list, in the style of RegisterViewList. A std::tuple inside a constant value that is made
// through a function pointer (the Make of Stack) does not compile with the current GCC, so the constant part of the builder uses this
// list, and only the run-time storage of the stack is a std::tuple.
template<typename... Entries>
struct List {
};


template<typename Head, typename... Tail>
struct List<Head, Tail...> {
	Head head;
	List<Tail...> tail;
};


// The list with entry added at the end.
template<typename Entry>
constexpr List<Entry> ListAppend(const List<>&, const Entry& entry) {
	return List<Entry>{entry, List<>{}};
}


template<typename Head, typename... Tail, typename Entry>
constexpr List<Head, Tail..., Entry> ListAppend(const List<Head, Tail...>& list, const Entry& entry) {
	return List<Head, Tail..., Entry>{list.head, ListAppend(list.tail, entry)};
}


// The type and access of entry I of a list.
template<std::size_t Index, typename List>
struct Nth;


template<typename Head, typename... Tail>
struct Nth<0, List<Head, Tail...>> {
	using type = Head;


	static constexpr Head& Of(List<Head, Tail...>& list) {
		return list.head;
	}


	static constexpr const Head& Of(const List<Head, Tail...>& list) {
		return list.head;
	}
};


template<std::size_t Index, typename Head, typename... Tail>
struct Nth<Index, List<Head, Tail...>> {
	using type = typename Nth<Index - 1, List<Tail...>>::type;


	static constexpr type& Of(List<Head, Tail...>& list) {
		return Nth<Index - 1, List<Tail...>>::Of(list.tail);
	}


	static constexpr const type& Of(const List<Head, Tail...>& list) {
		return Nth<Index - 1, List<Tail...>>::Of(list.tail);
	}
};


template<std::size_t Index, typename... Entries>
constexpr const typename Nth<Index, List<Entries...>>::type& ListAt(const List<Entries...>& list) {
	return Nth<Index, List<Entries...>>::Of(list);
}


template<std::size_t Index, typename... Entries>
constexpr typename Nth<Index, List<Entries...>>::type& ListAt(List<Entries...>& list) {
	return Nth<Index, List<Entries...>>::Of(list);
}


// A block that declares the member template Step has option methods that come right after it in a chain. The concept checks the name
// only; the Step is not instantiated here.
template<typename Block>
concept BlockHasStep = requires {
	typename Block::template Step<None>;
};


// The builder that follows a block. A block with a Step gives its Step over the next builder, so its option methods are callable right
// after it; a block without one gives the next builder.
template<typename Block, typename NextBuilder, bool HasStep = BlockHasStep<Block>>
struct StepOf {
	using type = NextBuilder;
};


template<typename Block, typename NextBuilder>
struct StepOf<Block, NextBuilder, true> {
	using type = typename Block::template Step<NextBuilder>;
};


// The options a block is given when it is added without options. A block with a Step is given its own Options type, default-constructed,
// so that its option methods have a value to change. Any other block is given none.
template<typename Block, bool HasStep = BlockHasStep<Block>>
struct DefaultOptionsOf {
	using type = None;
};


template<typename Block>
struct DefaultOptionsOf<Block, true> {
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
class Builder {
public:
	using Tuple = std::tuple<Entries...>;
	using LastEntry = typename LastBlockEntry<Entries...>::type;
	using LinkType = typename LinkOf<Entries...>::type;
	static constexpr std::size_t count = sizeof...(Entries);


	// The builder a block method returns: the next builder with the entry added, or the block's Step over it.
	template<typename Block, typename Entry>
	using BuilderAfter = typename StepOf<Block, Builder<Entries..., Entry>>::type;


	List<Entries...> entries;
	Settings settings;


	constexpr Builder() = default;


	constexpr Builder(List<Entries...> list, Settings values) : entries(list), settings(values) {
	}


	// The block methods take any type and check it first, so the message names the block's kind before the concept's notes.
	template<typename Block>
	constexpr Builder<Entries..., BlockEntry<BlockKind::link, Block, None>> Link() const {
		static_assert(LinkBlock<Block>, "the link does not satisfy VUB::LinkBlock: a link needs traits, and Write (a byte stream) or Send (packets)");
		return Append(BlockEntry<BlockKind::link, Block, None>{});
	}


	template<typename Block>
	constexpr BuilderAfter<Block, BlockEntry<BlockKind::framing, Block, typename DefaultOptionsOf<Block>::type>> Framing() const {
		static_assert(FramingBlock<Block>, "the framing does not satisfy VUB::FramingBlock: it needs usesTime, Growth(length), Frame and Push");
		using Options = typename DefaultOptionsOf<Block>::type;
		return Extend<Block>(BlockEntry<BlockKind::framing, Block, Options>{});
	}


	template<typename Block, typename Options>
	constexpr BuilderAfter<Block, BlockEntry<BlockKind::framing, Block, Options>> Framing(const Options& options) const {
		static_assert(FramingBlock<Block>, "the framing does not satisfy VUB::FramingBlock: it needs usesTime, Growth(length), Frame and Push");
		return Extend<Block>(BlockEntry<BlockKind::framing, Block, Options>{options});
	}


	template<typename Block>
	constexpr BuilderAfter<Block, BlockEntry<BlockKind::transform, Block, typename DefaultOptionsOf<Block>::type>> Transform() const {
		static_assert(TransformBlock<Block>, "the transform does not satisfy VUB::TransformBlock: it needs growth, fingerprint, Encode and Decode");
		using Options = typename DefaultOptionsOf<Block>::type;
		return Extend<Block>(BlockEntry<BlockKind::transform, Block, Options>{});
	}


	template<typename Block, typename Options>
	constexpr BuilderAfter<Block, BlockEntry<BlockKind::transform, Block, Options>> Transform(const Options& options) const {
		static_assert(TransformBlock<Block>, "the transform does not satisfy VUB::TransformBlock: it needs growth, fingerprint, Encode and Decode");
		return Extend<Block>(BlockEntry<BlockKind::transform, Block, Options>{options});
	}


	template<typename Block>
	constexpr BuilderAfter<Block, BlockEntry<BlockKind::layer, Block, typename DefaultOptionsOf<Block>::type>> Layer() const {
		static_assert(LayerBlock<Block>,
			"the layer does not satisfy VUB::LayerBlock: it needs header, fingerprint, usesTime, makesBulk, Add, Take and Process");
		using Options = typename DefaultOptionsOf<Block>::type;
		return Extend<Block>(BlockEntry<BlockKind::layer, Block, Options>{});
	}


	template<typename Block, typename Options>
	constexpr BuilderAfter<Block, BlockEntry<BlockKind::layer, Block, Options>> Layer(const Options& options) const {
		static_assert(LayerBlock<Block>,
			"the layer does not satisfy VUB::LayerBlock: it needs header, fingerprint, usesTime, makesBulk, Add, Take and Process");
		return Extend<Block>(BlockEntry<BlockKind::layer, Block, Options>{options});
	}


	// The largest application payload, in bytes. Required.
	constexpr Builder MaxPayload(std::size_t bytes) const {
		Builder next = *this;
		next.settings.maxPayload = bytes;
		return next;
	}


	// The bytes a byte-stream link's receive ring holds.
	constexpr Builder ReceiveRing(std::size_t bytes) const {
		Builder next = *this;
		next.settings.receiveRing = bytes;
		return next;
	}


	// The packets a packet link's receive queue holds.
	constexpr Builder ReceiveQueue(std::size_t packets) const {
		Builder next = *this;
		next.settings.receiveQueue = packets;
		return next;
	}


	// The packets the send side holds while the link is busy. Send returns busy when they are full.
	constexpr Builder SendQueue(std::size_t packets) const {
		Builder next = *this;
		next.settings.sendQueue = packets;
		return next;
	}


	// The most packets one Process call sends. A bulk layer that always has data cannot hold the call past this.
	constexpr Builder PerProcess(std::size_t packets) const {
		Builder next = *this;
		next.settings.perProcess = packets;
		return next;
	}


	// The clock: a function that returns microseconds as uint32. Wrap-safe differences are taken by the blocks.
	template<uint32 (*Fn)()>
	constexpr Builder Clock() const {
		Builder next = *this;
		next.settings.clockFn = Fn;
		return next;
	}


	// What the stack does with its packets while the connection is down: Wait (the default) keeps them queued, Fail drops them and refuses
	// further ones. Fail needs a link that reports its state or a layer that reports its peer; the builder checks it.
	constexpr Builder WhileDown(Down policy) const {
		Builder next = *this;
		next.settings.whileDown = policy;
		return next;
	}


	// The receive handler: called from Process with each packet that reached the top of the chain.
	template<void (*Fn)(const Meta&, std::span<const uint8>)>
	constexpr Builder OnReceive() const {
		Builder next = *this;
		next.settings.receiveFn = Fn;
		return next;
	}


	template<typename Block>
	static constexpr bool LastIs() {
		return std::is_same_v<typename LastEntry::BlockType, Block>;
	}


	constexpr typename LastEntry::OptionsType LastOptions() const {
		if constexpr (count > 0) {
			return ListAt<count - 1>(entries).options;
		} else {
			Require<"LastOptions() needs a block before it">(false);
			return {};
		}
	}


	// Replaces the options of the last block. The options must have the type that block was given.
	template<typename Options>
	constexpr Builder WithLastOptions(const Options& value) const {
		if constexpr (count > 0 && std::is_same_v<typename LastEntry::OptionsType, Options>) {
			Builder next = *this;
			ListAt<count - 1>(next.entries).options = value;
			return next;
		} else {
			Require<"WithLastOptions() needs a block before it, with options of the same type">(false);
			return *this;
		}
	}


	// The kind of each block, in chain order.
	static constexpr std::array<BlockKind, count> Kinds() {
		return std::array<BlockKind, count>{Entries::kind...};
	}


	// The index of the framing block, or count when there is none.
	static constexpr std::size_t FramingIndex() {
		const std::array<BlockKind, count> kinds = Kinds();
		for (std::size_t index = 0; index < count; index++) {
			if (kinds[index] == BlockKind::framing) {
				return index;
			}
		}

		return count;
	}


	// The number of layers and transforms: the blocks that change a packet on its way down.
	static constexpr std::size_t StepCount() {
		std::size_t total = 0;
		((total += (Entries::kind == BlockKind::layer || Entries::kind == BlockKind::transform) ? 1 : 0), ...);
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
		return StreamLink<LinkType>;
	}


	// Whether a layer of the chain declares its peer's state (reportsPeer).
	static constexpr bool PeersReport() {
		return (false || ... || Entries::ReportsPeer());
	}


	// Whether each block of the chain, in chain order, declares its peer (reportsPeer).
	static constexpr std::array<bool, count> PeerReports() {
		return std::array<bool, count>{Entries::ReportsPeer()...};
	}


	// Whether the chain can ever tell that the connection is down: the link reports its state, or a layer reports its peer.
	static constexpr bool CanReportDown() {
		return ChainLinkTraits().reportsState || PeersReport();
	}


	static constexpr LinkTraits ChainLinkTraits() {
		return TraitsOf<LinkType>();
	}


	// The largest packet the blocks build from a payload of settings.maxPayload bytes: the sum of the layer headers and transform growths.
	static constexpr std::size_t PacketBound(const Settings& values) {
		std::size_t bound = values.maxPayload;
		((bound = Entries::PacketAdd(bound)), ...);
		return bound;
	}


	// The largest framed packet on a byte-stream link: the packet bound with the framing's growth added.
	static constexpr std::size_t FramedBound(const Settings& values) {
		std::size_t bound = PacketBound(values);
		((bound = Entries::FrameAdd(bound)), ...);
		return bound;
	}


	// Checks the chain. Every rule that is not met is a compile error with its message.
	static constexpr bool Check(const Settings& values) {
		(CheckOptions<Entries>(), ...);

		// The lengths of a packet and of a framed packet are 16-bit. The size rules come first: a constant expression stops at its first
		// failed check, so a size error is reported before a link error that follows from the same size.
		Require<"a packet of 65536 bytes or more does not fit the 16-bit lengths: lower MaxPayload or the block headers and growths">(
			PacketBound(values) <= 65535);
		Require<"a framed packet of 65536 bytes or more does not fit the 16-bit lengths: lower MaxPayload or the framing growth">(
			FramedBound(values) <= 65535);

		const std::array<BlockKind, count> kinds = Kinds();
		std::size_t links = 0;
		std::size_t framings = 0;
		for (std::size_t index = 0; index < count; index++) {
			if (kinds[index] == BlockKind::link) {
				links++;
			}

			if (kinds[index] == BlockKind::framing) {
				framings++;
			}
		}

		Require<"a chain needs a link: add Link<Type>() as the last block">(links > 0);
		Require<"a chain has more than one link">(links <= 1);
		Require<"a chain has two framings">(framings <= 1);
		if constexpr (count > 0) {
			Require<"the link must be the last block of the chain">(kinds[count - 1] == BlockKind::link);
		}

		// A chain holds one layer that declares its peer, and it is the lowest layer: its state is the connection's, and the layers below
		// it, the link and the framing, are the ones it reports on. Two of them would overwrite each other's state. The count comes first,
		// so two layers report the count and not the position.
		const std::array<bool, count> reports = PeerReports();
		std::size_t peerLayers = 0;
		std::size_t lowestLayer = 0;
		for (std::size_t index = 0; index < count; index++) {
			if (reports[index]) {
				peerLayers++;
			}

			if (kinds[index] == BlockKind::layer) {
				lowestLayer = index;
			}
		}

		Require<"two layers declare reportsPeer: the stack keeps one peer state, and a second layer would overwrite the first">(
			peerLayers <= 1);
		Require<"a layer that declares reportsPeer must be the lowest layer of the chain: it is a hop-level block, below every other layer">(
			peerLayers == 0 || reports[lowestLayer]);

		constexpr LinkTraits traits = ChainLinkTraits();
		if constexpr (PacketLink<LinkType>) {
			Require<"a packet link takes no framing: remove the Framing<Type>() block">(framings == 0);
			Require<"a packet link must be Framed(): its packets have boundaries of their own">(traits.framed);
		} else if constexpr (StreamLink<LinkType>) {
			Require<"a stream link needs a framing: add Framing<Type>() right above the link">(framings == 1);
			Require<"the framing must be right above the link">(framings == 0 || (count >= 2 && FramingIndex() == count - 2));
			Require<"a stream link cannot be Framed(): its bytes have no packet boundaries">(!traits.framed);
		}

		if constexpr (LinkBlock<LinkType>) {
			Require<"the link must say HalfDuplex() or FullDuplex()">(traits.duplex != Duplex::Unset);
			Require<"a link has no MaxPacket: set MaxPacket(n) in its traits">(traits.maxPacket > 0);
			Require<"the MaxPacket of the link is 65536 bytes or more: the received lengths are 16-bit">(traits.maxPacket <= 65535);
			Require<"a packet is larger than the MaxPacket of the link: the payload and the headers and growths of the blocks add up to more">(
				PacketBound(values) <= traits.maxPacket);
		}

		Require<"a link with ReportsState() needs SetStateSink(VUB::StateSink): the stack gives its state object there">(
			!traits.reportsState || ReportingLink<LinkType>);
		Require<"a link has both SetReceiver and Poll: a link receives in an interrupt or is polled, not both, since two writers would share the receive ring or queue">(
			!(ReceivingLink<LinkType> && PollingLink<LinkType>));
		Require<"WhileDown(VUB::Down::Fail) needs a connection that can go down: the link needs ReportsState(), or a layer needs reportsPeer">(
			values.whileDown != Down::Fail || CanReportDown());

		// A ring or a queue of n slots needs n + 1 indexes of at most 65535, so it holds at most 65535 entries.
		Require<"ReceiveRing() is zero">(values.receiveRing > 0);
		Require<"the receive ring holds at most 65535 bytes: its indexes are 16-bit">(values.receiveRing <= 65535);
		Require<"ReceiveQueue() is zero">(values.receiveQueue > 0);
		Require<"the receive queue holds at most 65535 packets: its indexes are 16-bit">(values.receiveQueue <= 65535);
		Require<"SendQueue() is zero">(values.sendQueue > 0);
		Require<"the send queue holds at most 65535 packets: its indexes are 16-bit">(values.sendQueue <= 65535);
		Require<"MaxPayload() is not given or is zero: set the largest application payload">(values.maxPayload > 0);
		Require<"OnReceive<&Fn>() is missing: the receive handler takes each packet">(values.receiveFn != nullptr);
		Require<"PerProcess() is zero">(values.perProcess > 0);
		Require<"a block needs time and the chain has no clock: add Clock<&Fn>() to the builder">(!NeedsTime() || values.clockFn != nullptr);
		return true;
	}


private:
	// A block given options must be built from them, unless it declares Configured: then its options are template arguments and it needs no
	// constructor. The message names the block's kind; a link and a block with no options pass.
	template<typename Entry>
	static constexpr void CheckOptions() {
		if constexpr (Entry::kind == BlockKind::layer && !std::is_same_v<typename Entry::OptionsType, None> && !DeclaresConfigured<Entry>) {
			Require<"the layer cannot be built from its options: give it a constructor that takes them">(
				std::is_constructible_v<typename Entry::BlockType, const typename Entry::OptionsType&>);
		} else if constexpr (Entry::kind == BlockKind::transform && !std::is_same_v<typename Entry::OptionsType, None>
				&& !DeclaresConfigured<Entry>) {
			Require<"the transform cannot be built from its options: give it a constructor that takes them">(
				std::is_constructible_v<typename Entry::BlockType, const typename Entry::OptionsType&>);
		} else if constexpr (Entry::kind == BlockKind::framing && !std::is_same_v<typename Entry::OptionsType, None>
				&& !DeclaresConfigured<Entry>) {
			Require<"the framing cannot be built from its options: give it a constructor that takes them">(
				std::is_constructible_v<typename Entry::BlockType, const typename Entry::OptionsType&>);
		}
	}


	template<typename Entry>
	constexpr Builder<Entries..., Entry> Append(Entry entry) const {
		return Builder<Entries..., Entry>(ListAppend(entries, entry), settings);
	}


	// Adds the entry of a block and returns the builder after it. A block with a Step gets the Step, so its option methods follow.
	template<typename Block, typename Entry>
	constexpr BuilderAfter<Block, Entry> Extend(const Entry& entry) const {
		using Next = Builder<Entries..., Entry>;
		using AfterBlock = BuilderAfter<Block, Entry>;
		if constexpr (std::is_same_v<AfterBlock, Next>) {
			return Append(entry);
		} else {
			return AfterBlock(Append(entry));
		}
	}
};

}
