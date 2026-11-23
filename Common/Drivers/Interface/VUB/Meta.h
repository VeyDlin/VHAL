#pragma once
#include <Utilities/DataTypes.h>

namespace VUB {


// The addresses and flags of a packet, as the blocks see them. The block that makes a packet sets the flags; every block below it
// reads them through the const reference it is given. Addresses are added in a later stage.
struct Meta {
	// The packet needs no delivery acknowledgement (the large-data layer marks its parts with it).
	static constexpr uint8 NoAcknowledgeFlag = 0x01;


	uint8 flags = 0;
};

}
