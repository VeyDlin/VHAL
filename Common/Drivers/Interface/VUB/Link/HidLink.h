#pragma once
#include <Drivers/Interface/VUB/Link/HidReport.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// A packet link over a HID function (Common/Drivers/Interface/USB/Class/HID/HidFunction.h). The template takes the function type, so
// any function with the same members works: SendReport, SetOutputDelegate and the Delegate type OutputDelegate.
//
// The link is a packet link, framed by its reports, and not reliable: the host may drop an input report it does not read. A report
// has the layout of HidReport.h: byte 0 is the payload's length, the payload follows, and the rest is zero. The report is ReportBytes
// long, and the function's input report must be that length, so the packet is at most ReportBytes - 1 bytes. The link has no state
// of its own: it is always up, and a Send that the function refuses while it is not configured is busy.
//
// Send packs the packet into the report and calls SendReport. A function with its queue full, or not configured yet, is busy, and the
// stack keeps the packet. Output reports arrive in the function's interrupt through its output delegate. Each one with a payload is
// given to the receiver as a packet; a report that is not a packet is dropped.
template<typename Hid, std::size_t ReportBytes = 64>
class HidLink {
	static_assert(ReportBytes >= 2 && ReportBytes <= 256, "a HID report holds a length byte and up to 255 payload bytes");

public:
	static constexpr LinkTraits traits = LinkTraits().Framed().MaxPacket(ReportBytes - 1).FullDuplex().Starts(Initiator::Either);


private:
	Hid* function;
	Receiver receiver;
	std::array<uint8, ReportBytes> report{};
	ResultStatus receiveStatus = ResultStatus::ok;


public:
	explicit HidLink(Hid& hidFunction) : function(&hidFunction) {
	}


	// The output delegate captures this link, so it stays where it is.
	HidLink(const HidLink&) = delete;


	HidLink& operator=(const HidLink&) = delete;


	// The stack gives its receiver here in its constructor, and an empty one in its destructor. An attached receiver sets the output
	// delegate; an empty one clears it. The function refuses a delegate once it is configured, and that status is kept.
	void SetReceiver(Receiver value) {
		receiver = value;
		if (receiver.Attached()) {
			receiveStatus = function->SetOutputDelegate(Hid::OutputDelegate::template Bind<HidLink, &HidLink::OnOutput>(this));
		} else {
			receiveStatus = function->SetOutputDelegate(typename Hid::OutputDelegate());
		}
	}


	// The status of the last SetReceiver: ok, or busy when the function was already configured.
	ResultStatus ReceiveStatus() const {
		return receiveStatus;
	}


	// Sends one packet as one input report. notReady and busy both come back as busy, so the stack keeps the packet and sends it
	// again. A packet that does not fit in the report is refused with invalidParameter.
	ResultStatus Send(std::span<const uint8> packet) {
		Result<std::size_t> packed = HidReportPack(packet, std::span<uint8>(report.data(), report.size()));
		if (packed.IsErr()) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = function->SendReport(std::span<const uint8>(report.data(), report.size()));
		if (status == ResultStatus::notReady) {
			return ResultStatus::busy;
		}

		return status;
	}


private:
	// Runs in the function's interrupt: the payload of an output report goes to the receiver as one packet.
	void OnOutput(std::span<const uint8> output) {
		Result<std::span<const uint8>> payload = HidReportPayload(output);
		if (payload.IsOk()) {
			receiver.FeedPacket(payload.Value());
		}
	}
};

}
