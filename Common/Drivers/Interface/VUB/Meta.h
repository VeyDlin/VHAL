#pragma once
#include <Utilities/DataTypes.h>

namespace VUB {


// The addresses and flags of a packet, as the blocks see them. The block that makes a packet sets the flags; every block below it
// reads them through the const reference it is given. The layer does not address its peers.
struct Meta {
	// The packet needs no delivery acknowledgement (the large-data layer marks its parts with it).
	static constexpr uint8 NoAcknowledgeFlag = 0x01;

	// The packet is a copy that a layer below sends again when the connection returns, from the copy the layer keeps. The stack drops the
	// copies with this flag that wait in its queues when the connection returns.
	static constexpr uint8 ResentBelowFlag = 0x02;


	uint8 flags = 0;
};

}
