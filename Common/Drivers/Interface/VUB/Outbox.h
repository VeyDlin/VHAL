#pragma once
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

namespace VUB {


// The way a layer sends a packet of its own from Process. The packet is complete at the layer's level: the layer added its header
// to it, and the blocks below the layer add theirs. The stack queues it on the control lane, so it goes before bulk traffic.
// Send returns busy when the control queue is full; the layer keeps the packet and tries again on the next Process. While the connection
// is down under the Fail option, Send returns noConnection, and the layer keeps the packet as well.
//
// SetPeer declares the peer up or down. The stack applies it after the layers have run, as a change of the connection. Only a layer that
// declares reportsPeer = true has an effect: its outbox is the one that reaches the stack. For any other layer the call does nothing.
//
// Deliver hands a packet up the receive path from the layer's own position. The packet is the one the layer would pass up: its header is
// removed, and the blocks above the layer decode it and take it as they take a packet the layer had just taken. It reaches the receive
// handler when they have. A layer that holds packets and releases them from Process declares passesUp = true. Deliver returns
// messageTooLong when the packet is longer than the link's receive buffer, and notSupported for a layer that did not declare passesUp.
//
// LinkUp tells whether the physical link is up, as of this Process. A link that does not report its state is always up. A layer that
// declares usesLinkState = true may ask; the stack gives the query only to such a layer on a link that reports its state, and any other
// layer gets a query that answers true.
class Outbox {
public:
	Outbox(
		void* ownerObject,
		ResultStatus (*sendFunction)(void*, std::span<const uint8>, const Meta&),
		void (*peerFunction)(void*, bool),
		ResultStatus (*deliverFunction)(void*, std::span<const uint8>),
		bool (*linkFunction)(void*) = nullptr) :
		owner(ownerObject), sendFn(sendFunction), peerFn(peerFunction), deliverFn(deliverFunction), linkFn(linkFunction) {
	}


	ResultStatus Send(std::span<const uint8> packet, const Meta& meta = Meta()) const {
		return sendFn(owner, packet, meta);
	}


	bool LinkUp() const {
		if (linkFn == nullptr) {
			return true;
		}

		return linkFn(owner);
	}


	// A stack gives no peer function to a layer that does not declare its peer, so the call does nothing there.
	void SetPeer(bool up) const {
		if (peerFn != nullptr) {
			peerFn(owner, up);
		}
	}


	// A stack gives no deliver function to a layer that does not declare passesUp, so the call is refused there.
	ResultStatus Deliver(std::span<const uint8> packet) const {
		if (deliverFn == nullptr) {
			return ResultStatus::notSupported;
		}

		return deliverFn(owner, packet);
	}


private:
	void* owner;
	ResultStatus (*sendFn)(void*, std::span<const uint8>, const Meta&);
	void (*peerFn)(void*, bool);
	ResultStatus (*deliverFn)(void*, std::span<const uint8>);
	bool (*linkFn)(void*);
};

}
