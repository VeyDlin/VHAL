#pragma once
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>


// The way a layer sends a packet of its own from Process. The packet is complete at the layer's level: the layer added its header
// to it, and the blocks below the layer add theirs. The stack queues it on the control lane, so it goes before bulk traffic.
// Send returns busy when the control queue is full; the layer keeps the packet and tries again on the next Process. While the connection
// is down under the Fail option, Send returns noConnection, and the layer keeps the packet as well.
//
// SetPeer declares the peer up or down. The stack applies it after the layers have run, as a change of the connection. Only a layer that
// declares reportsPeer = true has an effect: its outbox is the one that reaches the stack. For any other layer the call does nothing.
class VubOutbox {
public:
	VubOutbox(
		void* ownerObject,
		ResultStatus (*sendFunction)(void*, std::span<const uint8>, const VubMeta&),
		void (*peerFunction)(void*, bool)) :
		owner(ownerObject), sendFn(sendFunction), peerFn(peerFunction) {
	}


	ResultStatus Send(std::span<const uint8> packet, const VubMeta& meta = VubMeta()) const {
		return sendFn(owner, packet, meta);
	}


	// A stack gives no peer function to a layer that does not declare its peer, so the call does nothing there.
	void SetPeer(bool up) const {
		if (peerFn != nullptr) {
			peerFn(owner, up);
		}
	}


private:
	void* owner;
	ResultStatus (*sendFn)(void*, std::span<const uint8>, const VubMeta&);
	void (*peerFn)(void*, bool);
};
