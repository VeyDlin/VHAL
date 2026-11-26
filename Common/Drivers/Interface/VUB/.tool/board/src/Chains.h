#pragma once
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Framing/LengthPrefix.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/PC/BulkLink.h>
#include <Drivers/Interface/VUB/PC/HidLink.h>
#include <Drivers/Interface/VUB/PC/SerialLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <chrono>
#include <cstddef>
#include <span>
#include <vector>
#include "LossyLink.h"


// The four chains of the board check. Each one is the chain of the same link on the device (Application/DemoVub in .demo/STM32G4), with the
// PC link in its place: the UART and CDC chains use COBS (the UART also CRC-16/MODBUS), the bulk chain LengthPrefix<2>, and the HID chain no
// framing. Each has VUB::Reliable with the device's options and MaxPayload 32. The link is wrapped in LossyStream or LossyPacket, which
// drop nothing unless --loss is given.
namespace VubBoard {
	constexpr std::size_t ChainMaxPayload = 32;


	// The results of the run, written by the stack's handlers from Process. One link runs per invocation, so one set of handlers serves
	// all the chains.
	inline std::vector<std::vector<uint8>> received;
	inline std::size_t outcomesOk = 0;
	inline std::size_t outcomesFailed = 0;
	inline std::size_t retried = 0;
	inline std::size_t restarts = 0;


	// The handlers are plain functions, not inline: the stacks take their addresses as template arguments.
	void OnEcho(const VUB::Meta&, std::span<const uint8> payload) {
		received.emplace_back(payload.begin(), payload.end());
	}


	void OnOutcome(const VUB::Outcome& outcome) {
		if (outcome.status == ResultStatus::ok) {
			outcomesOk++;
		} else {
			outcomesFailed++;
		}

		if (outcome.attempts > 1) {
			retried++;
		}
	}


	void OnRestart() {
		restarts++;
	}


	// Microseconds of the steady clock, as the stack's clock.
	uint32 ClockUs() {
		std::chrono::microseconds now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
		return static_cast<uint32>(now.count());
	}


	using UartLinkType = LossyStream<VUB::PC::SerialLink, CobsFrames>;
	using CdcLinkType = LossyStream<VUB::PC::SerialLink, CobsFrames>;
	using BulkLinkType = LossyStream<VUB::PC::BulkLink, LengthPrefixFrames>;
	using HidLinkType = LossyPacket<VUB::PC::HidLink<64>>;


	// The chain of the device's UART: CRC-16/MODBUS over the payload, then COBS. The link is a template argument, so the host loopback
	// can run the same chain over its own link.
	template<typename LinkType>
	struct UartChainOf {
		using Type = VUB::Stack<+[] {
			return VUB::Builder()
				.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
				.OnOutcome<&OnOutcome>()
				.OnRestart<&OnRestart>()
				.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
				.Framing<VUB::Cobs>()
				.Link<LinkType>()
				.MaxPayload(32)
				.template OnReceive<&OnEcho>()
				.template Clock<&ClockUs>();
		}>;
	};


	using UartChain = UartChainOf<UartLinkType>::Type;


	// The chain of the device's CDC-ACM serial port: COBS.
	using CdcChain = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.OnOutcome<&OnOutcome>()
			.OnRestart<&OnRestart>()
			.Framing<VUB::Cobs>()
			.Link<CdcLinkType>()
			.MaxPayload(32)
			.OnReceive<&OnEcho>()
			.Clock<&ClockUs>();
	}>;


	// The chain of the device's vendor bulk interface: a two-byte length prefix.
	using BulkChain = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.OnOutcome<&OnOutcome>()
			.OnRestart<&OnRestart>()
			.Framing<VUB::LengthPrefix<2>>()
			.Link<BulkLinkType>()
			.MaxPayload(32)
			.OnReceive<&OnEcho>()
			.Clock<&ClockUs>();
	}>;


	// The chain of the device's vendor HID interface: no framing, a report is a packet.
	using HidChain = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.OnOutcome<&OnOutcome>()
			.OnRestart<&OnRestart>()
			.Link<HidLinkType>()
			.MaxPayload(32)
			.OnReceive<&OnEcho>()
			.Clock<&ClockUs>();
	}>;
}
