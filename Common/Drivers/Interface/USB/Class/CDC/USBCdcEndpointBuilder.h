#pragma once
#include <cstddef>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// The part the CDC class builders share: the notification IN endpoint and the bulk IN and bulk OUT endpoints of the data path, with their
// address checks, their interval and the endpoint requests the configuration builder takes. USBCdcAcmBuilder and USBCdcNcmBuilder derive
// from it with themselves as TBuilder, so the setters they inherit return the derived builder and a chain keeps its type.
//
// The endpoint order is the one the functions read: endpoint 0 is the notification IN (interrupt), endpoint 1 the bulk IN and endpoint 2
// the bulk OUT. An address left out is assigned by the configuration builder, in that order and so the numbers the other endpoints of the
// device do not use. The notification packet is 16 bytes in both functions, and the bulk packets are 64 bytes (full speed).
template<typename TBuilder>
class USBCdcEndpointBuilder {
public:
	// The notification packet size. The descriptor and the function must agree on it, so it is a constant, not an option.
	static constexpr uint16 NotificationPacketSize = 16;
	// The bulk packet size of both data endpoints (full speed).
	static constexpr uint16 BulkPacketSize = 64;
	// The interval of the notification endpoint when IntervalMs() is not called.
	static constexpr uint8 DefaultIntervalMs = 16;


private:
	uint8 notificationAddress = 0;
	uint8 bulkInAddress = 0;
	uint8 bulkOutAddress = 0;
	uint8 intervalMs = DefaultIntervalMs;


public:
	// Address of the notification endpoint, an interrupt IN. Without it the configuration builder assigns the number.
	constexpr TBuilder& NotificationIn(uint8 address) {
		Require<"NotificationIn needs an IN address (bit 7 set)">(USBEndpoint::IsIn(address));
		Require<"endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
		notificationAddress = address;
		return static_cast<TBuilder&>(*this);
	}


	// Address of the bulk IN endpoint, the device to host direction. Without it the configuration builder assigns the number.
	constexpr TBuilder& BulkIn(uint8 address) {
		Require<"BulkIn needs an IN address (bit 7 set)">(USBEndpoint::IsIn(address));
		Require<"endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
		bulkInAddress = address;
		return static_cast<TBuilder&>(*this);
	}


	// Address of the bulk OUT endpoint, the host to device direction. Without it the configuration builder assigns the number.
	constexpr TBuilder& BulkOut(uint8 address) {
		Require<"BulkOut needs an OUT address (bit 7 clear)">(!USBEndpoint::IsIn(address));
		Require<"endpoint 0 belongs to the control pipe">(USBEndpoint::Number(address) != 0);
		bulkOutAddress = address;
		return static_cast<TBuilder&>(*this);
	}


	// Polling interval of the notification endpoint, in milliseconds. 1 to 255. Default 16.
	constexpr TBuilder& IntervalMs(uint8 milliseconds) {
		Require<"IntervalMs is 1 to 255 ms">(milliseconds >= 1);
		intervalMs = milliseconds;
		return static_cast<TBuilder&>(*this);
	}


	constexpr std::size_t InterfaceCount() const {
		return 2;
	}


	constexpr std::size_t EndpointCount() const {
		return 3;
	}


	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		USBEndpointRequest request;
		if (index == 0) {
			request.type = USBEndpointType::Interrupt;
			request.direction = USBDirection::In;
			request.address = notificationAddress;
			request.maxPacketSize = NotificationPacketSize;
			request.interval = intervalMs;
			return request;
		}

		request.type = USBEndpointType::Bulk;
		request.direction = index == 1 ? USBDirection::In : USBDirection::Out;
		request.address = index == 1 ? bulkInAddress : bulkOutAddress;
		request.maxPacketSize = BulkPacketSize;
		request.interval = 0;
		return request;
	}
};
