#pragma once
#include <Drivers/Interface/VUB/Link/HidReport.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// The largest report a function of type Hid takes: its MaxReportLength when the type has one, otherwise 256, the most a length byte
// describes.
template<typename Hid>
consteval std::size_t HidMaxReportBytes() {
	if constexpr (requires { Hid::MaxReportLength; }) {
		return Hid::MaxReportLength;
	} else {
		return 256;
	}
}


// A packet link over a HID function (Common/Drivers/Interface/USB/Class/HID/HidFunction.h). The template takes the function type, so
// any type with the same members works. The link uses these members of the function: SendReport, SetOutputDelegate, the Delegate type
// OutputDelegate, and Report(), whose InputLength(0) and OutputLength(0) are the lengths of the reports without a report ID.
// MaxReportLength, when the type has it, bounds ReportBytes at compile time; HidFunction has it at 64.
//
// The link is a packet link, framed by its reports, and not reliable: the host may drop an input report it does not read. A report
// has the layout of HidReport.h: byte 0 is the payload's length, the payload follows, and the rest is zero. The report is ReportBytes
// long, and the function's input and output reports must be that length, so the packet is at most ReportBytes - 1 bytes. The link has
// no state of its own: it is always up, and a Send that the function refuses while it is not configured is busy.
//
// Send packs the packet into the report and calls SendReport. A function with its queue full, or not configured yet, is busy, and the
// stack keeps the packet. Output reports arrive in the function's interrupt through its output delegate. Each one with a payload is
// given to the receiver as a packet; a report that is not a packet is dropped. SetReceiver refuses an attached receiver, with
// invalidParameter as its status, when the function's reports are not ReportBytes long.
template<typename Hid, std::size_t ReportBytes = 64>
class HidLink {
	static_assert(ReportBytes >= 2, "a HID report holds a length byte and at least one payload byte");
	static_assert(ReportBytes <= 256, "a HID report holds a length byte and up to 255 payload bytes");
	static_assert(ReportBytes <= HidMaxReportBytes<Hid>(), "a HID report is no longer than the function's reports");

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
	// delegate; an empty one clears it. The function refuses a delegate once it is configured, and that status is kept. When the
	// function is configured, the delegate of a detached link stays, and its reports then find an empty receiver, which drops them.
	void SetReceiver(Receiver value) {
		if (!value.Attached()) {
			receiveStatus = function->SetOutputDelegate(typename Hid::OutputDelegate());
			SetReceiverValue(value);
			return;
		}

		if (!ReportLengthsMatch()) {
			receiveStatus = ResultStatus::invalidParameter;
			return;
		}

		SetReceiverValue(value);
		receiveStatus = function->SetOutputDelegate(Hid::OutputDelegate::template Bind<HidLink, &HidLink::OnOutput>(this));
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
	// The receiver is read in the function's interrupt, so it is written in a critical section.
	void SetReceiverValue(Receiver value) {
		System::CriticalSection(true);
		receiver = value;
		System::CriticalSection(false);
	}


	// True when the function's input and output reports without a report ID are ReportBytes long.
	bool ReportLengthsMatch() const {
		return function->Report().InputLength(0) == ReportBytes && function->Report().OutputLength(0) == ReportBytes;
	}


	// Runs in the function's interrupt: the payload of an output report goes to the receiver as one packet.
	void OnOutput(std::span<const uint8> output) {
		Result<std::span<const uint8>> payload = HidReportPayload(output);
		if (payload.IsOk()) {
			receiver.FeedPacket(payload.Value());
		}
	}
};

}
