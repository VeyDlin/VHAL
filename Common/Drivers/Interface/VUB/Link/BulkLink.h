#pragma once
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// A byte-stream link over a vendor bulk function in buffered mode (Common/Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h). The
// template takes the function type, so any function with the same members works: Write, Read and IsConfigured.
//
// The link is a stream, reliable and full duplex, and it reports its state. A vendor function has no DTR, so the link is up while the
// function is configured. Otherwise it is as the CDC link: the state is sampled in Poll, Poll reads up to the receiver's room, and Write
// is the function's Write.
//
// MaxPacketBytes is the largest packet of the link, which is the stack's buffer for a packet of the framing above it.
template<typename Vendor, std::size_t MaxPacketBytes = 64>
class BulkLink {
	static_assert(MaxPacketBytes > 0, "the largest packet of a bulk link is at least one byte");

public:
	static constexpr LinkTraits traits = LinkTraits().MaxPacket(MaxPacketBytes).Reliable().FullDuplex().ReportsState().Starts(Initiator::Either);


private:
	static constexpr std::size_t readChunkBytes = 64;

	Vendor* function;
	StateSink sink;
	bool reportedUp = false;
	std::array<uint8, readChunkBytes> readBuffer{};


public:
	explicit BulkLink(Vendor& vendorFunction) : function(&vendorFunction) {
	}


	// The stack gives its state sink here. The link reports its state at once, so the stack knows it from the first Process.
	void SetStateSink(StateSink value) {
		sink = value;
		reportedUp = IsUp();
		sink.Report(reportedUp);
	}


	// Samples the state, then reads what the host sent, up to the receiver's room, and feeds it to the receiver.
	void Poll(Receiver receiver) {
		SampleState();
		std::size_t room = receiver.Room();
		while (room > 0) {
			std::size_t take = room < readChunkBytes ? room : readChunkBytes;
			std::size_t got = function->Read(std::span<uint8>(readBuffer.data(), take));
			if (got == 0) {
				return;
			}

			receiver.Feed(std::span<const uint8>(readBuffer.data(), got));
			room = room - got;
		}
	}


	// Queues the bytes the function takes. Returns 0 while the function does not take any.
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return static_cast<std::size_t>(function->Write(bytes));
	}


private:
	bool IsUp() const {
		return function->IsConfigured();
	}


	void SampleState() {
		bool up = IsUp();
		if (up != reportedUp) {
			reportedUp = up;
			sink.Report(up);
		}
	}
};

}
