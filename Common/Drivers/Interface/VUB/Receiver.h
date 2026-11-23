#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>

namespace VUB {


// The way a link hands received data to its stack: an owner object and function pointers, as StateSink is. The stack gives one to a
// link that receives in an interrupt (SetReceiver), or as the argument of Poll for a link that reads on request. Feed takes bytes of a
// byte-stream link and FeedPacket one whole packet of a packet link. Room is how much the stack takes at the time of the call: the
// free bytes of the receive ring, or the free slots of the receive queue. The data goes through the stack's Feed and FeedPacket, so a
// byte or a packet that does not fit counts as it does there. A receiver of the other kind, and a default one, drop what they are given
// and report no room.
class Receiver {
public:
	Receiver() = default;


	Receiver(
		void* ownerObject,
		void (*feedBytesFunction)(void*, std::span<const uint8>),
		void (*feedPacketFunction)(void*, std::span<const uint8>),
		std::size_t (*roomFunction)(void*)
	)
	{
		owner = ownerObject;
		feedBytesFn = feedBytesFunction;
		feedPacketFn = feedPacketFunction;
		roomFn = roomFunction;
	}


	// Gives bytes of a byte-stream link to the stack, one by one, as its Feed takes them.
	void Feed(std::span<const uint8> bytes) const {
		if (feedBytesFn != nullptr) {
			feedBytesFn(owner, bytes);
		}
	}


	// Gives one whole packet of a packet link to the stack, as its FeedPacket takes it.
	void FeedPacket(std::span<const uint8> packet) const {
		if (feedPacketFn != nullptr) {
			feedPacketFn(owner, packet);
		}
	}


	// True when the receiver has a stack behind it. A default receiver, or the one a destroyed stack left in its link, is not attached.
	bool Attached() const {
		return feedBytesFn != nullptr || feedPacketFn != nullptr;
	}


	// How many bytes (a byte-stream link) or packets (a packet link) the stack takes at the time of the call.
	std::size_t Room() const {
		if (roomFn != nullptr) {
			return roomFn(owner);
		}

		return 0;
	}


private:
	void* owner = nullptr;
	void (*feedBytesFn)(void*, std::span<const uint8>) = nullptr;
	void (*feedPacketFn)(void*, std::span<const uint8>) = nullptr;
	std::size_t (*roomFn)(void*) = nullptr;
};


// Gives the receiver back to the link when the stack is destroyed, so the link does not feed a stack that is gone. A link that does not
// take a receiver has none, and this holds nothing.
template<bool Enabled, typename Link>
class ReceiverDetach {
public:
	explicit ReceiverDetach(Link* linkObject) : link(linkObject) {
	}


	ReceiverDetach(const ReceiverDetach&) = delete;
	ReceiverDetach& operator=(const ReceiverDetach&) = delete;


	~ReceiverDetach() {
		link->SetReceiver(Receiver());
	}


private:
	Link* link;
};


template<typename Link>
class ReceiverDetach<false, Link> {
public:
	explicit ReceiverDetach(Link*) {
	}
};

}
