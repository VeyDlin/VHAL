#pragma once
#include "Chains.h"
#include "RequestCheck.h"
#include <Drivers/Interface/VUB/.tool/common/ActuatorCommands.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// The two chains of the requests check. Each one is the chain of the same link on the device (Application/DemoVub with
// VHAL_VUB_MODE=Requests): the requests layer over Reliable with MaxPayload 32, and the framing and the transform of the link. The
// link types are the ones of Chains.h, so --loss wraps them the same way. The requester has eight requests open at once, pings with
// up to 16 bytes of padding and a deadline of PingTimeoutMs, and the layer counts its refusals, late replies and lost events.
namespace VubBoard {
	// The payloads the requests layer does not take. The check does not use them, so they are only counted.
	inline std::size_t requestUnclaimed = 0;


	void OnRequestPayload(const VUB::Meta&, std::span<const uint8>) {
		requestUnclaimed++;
	}


	// The chain of the device's UART: CRC-16/MODBUS over the payload, then COBS. The link is a template argument (see UartChainOf).
	template<typename LinkType>
	struct UartRequestsChainOf {
		using Type = VUB::Stack<+[] {
			return VUB::Builder()
				.Layer<VUB::Requests<Actuator::ActuatorCommands, RequestsCheck::PcHandlers>>()
					.Open(8)
					.Ping(RequestsCheck::PingPadding)
					.BuiltInTimeoutMs(RequestsCheck::PingTimeoutMs)
					.Counted()
				.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
				.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
				.Framing<VUB::Cobs>()
				.Link<LinkType>()
				.MaxPayload(32)
				.template OnReceive<&OnRequestPayload>()
				.template Clock<&ClockUs>();
		}>;
	};


	using UartRequestsChain = UartRequestsChainOf<UartLinkType>::Type;


	// The chain of the device's CDC-ACM serial port: COBS.
	using CdcRequestsChain = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Requests<Actuator::ActuatorCommands, RequestsCheck::PcHandlers>>()
				.Open(8)
				.Ping(RequestsCheck::PingPadding)
				.BuiltInTimeoutMs(RequestsCheck::PingTimeoutMs)
				.Counted()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.Framing<VUB::Cobs>()
			.Link<CdcLinkType>()
			.MaxPayload(32)
			.OnReceive<&OnRequestPayload>()
			.Clock<&ClockUs>();
	}>;
}
