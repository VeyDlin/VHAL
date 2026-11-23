#pragma once
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>


// The way a layer sends a packet of its own from Process. The packet is complete at the layer's level: the layer added its header
// to it, and the blocks below the layer add theirs. The stack queues it on the control lane, so it goes before bulk traffic.
// Send returns busy when the control queue is full; the layer keeps the packet and tries again on the next Process.
class VubOutbox {
public:
	VubOutbox(void* ownerObject, ResultStatus (*sendFunction)(void*, std::span<const uint8>, const VubMeta&)) :
		owner(ownerObject), sendFn(sendFunction) {
	}


	ResultStatus Send(std::span<const uint8> packet, const VubMeta& meta = VubMeta()) const {
		return sendFn(owner, packet, meta);
	}


private:
	void* owner;
	ResultStatus (*sendFn)(void*, std::span<const uint8>, const VubMeta&);
};
