#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Class/Common/BulkEndpointEngine.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <array>
#include <span>


// Shared part of the vendor bulk functions: one bulk IN and one bulk OUT endpoint, 64-byte packets, double-buffered when the port
// allows it. The transfer logic is in BulkEndpointEngine (Class/Common), which this class owns privately; this class checks the
// vendor interface and its endpoints, and forwards the USBFunction callbacks to the engine. The data path is in the two derived
// templates: VendorBulkFunction (buffered) and VendorBulkStreamFunction (streaming).
//
// Descriptors are not built here. The application composes them with the builders in Descriptor/. For this function it
// writes into its configuration:
//   - one interface descriptor: bInterfaceNumber as passed in the interfaces span (exactly one interface),
//     bInterfaceClass 0xFF (Vendor), bNumEndpoints 2; bInterfaceSubClass and bInterfaceProtocol are the application's;
//   - one bulk IN and one bulk OUT endpoint descriptor, with the two addresses passed in the endpoints span (in either
//     order), wMaxPacketSize 64, bInterval 0. Use 0x81 and 0x02: they are two endpoint numbers, so a port that supports
//     double buffering gives both directions of the pair double buffering. See USBDeviceAdapter::OpenEndpoint() for ports
//     that share one register between the two directions of a number.
//
// The engine's rules are described in BulkEndpointEngine.h: the IN queue of two stages, the thread claim, the halt, reset and
// session handling. Callbacks run in interrupt context, and Process() runs from the application's loop or thread.
class VendorBulkFunctionBase : public USBFunction, private BulkEndpointEngine {
public:
	// Full-speed bulk packet size of both endpoints. A transfer of N packets is N * PacketSize bytes.
	static constexpr uint16 PacketSize = BulkEndpointEngine::PacketSize;
	// Number of IN stages. The derived template supplies this many transfer buffers.
	static constexpr uint8 InStageCount = BulkEndpointEngine::InStageCount;


public:
	VendorBulkFunctionBase(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: USBFunction(interfaces, endpoints) {
	}


	// The engine keeps spans into the derived object, so a copy or a move would leave them pointing into the source.
	VendorBulkFunctionBase(const VendorBulkFunctionBase&) = delete;


	VendorBulkFunctionBase& operator=(const VendorBulkFunctionBase&) = delete;


	VendorBulkFunctionBase(VendorBulkFunctionBase&&) = delete;


	VendorBulkFunctionBase& operator=(VendorBulkFunctionBase&&) = delete;


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		if (GetInterfaces().size() != 1) {
			return ResultStatus::invalidParameter;
		}

		bool hasIn = false;
		bool hasOut = false;
		uint8 newInAddress = 0;
		uint8 newOutAddress = 0;
		for (uint8 address : GetEndpoints()) {
			if (!USBEndpoint::IsValid(address) || USBEndpoint::Number(address) == 0) {
				return ResultStatus::invalidParameter;
			}

			if (USBEndpoint::IsIn(address)) {
				if (hasIn) {
					return ResultStatus::invalidParameter;
				}

				hasIn = true;
				newInAddress = address;
			} else {
				if (hasOut) {
					return ResultStatus::invalidParameter;
				}

				hasOut = true;
				newOutAddress = address;
			}
		}

		if (!hasIn || !hasOut) {
			return ResultStatus::invalidParameter;
		}

		return BeginSession(context, newInAddress, newOutAddress);
	}


	// The core calls this inside Stop(), which the caller wraps in a critical section. It also calls it on SET_CONFIGURATION(0), on a
	// new configuration (the old one is dropped first) and when a configuration fails part way. See BulkEndpointEngine::EndSession().
	void OnDeconfigured() override {
		EndSession();
	}


	void OnBusReset() override {
		ResetSession();
	}


	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		HandleTransferComplete(address, byteCount, context);
	}


	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		HandleEndpointHalt(address, halted, context);
	}


	void Process(USBDeviceContext& context) override {
		Pump(context);
	}


protected:
	// The transfer buffers, as the derived classes have always named them.
	using BulkEndpointEngine::inStages;
	using BulkEndpointEngine::outStage;


	// Gives the engine its transfer buffers. The derived template calls it once, in its constructor.
	void SetBulkStages(const std::array<std::span<uint8>, InStageCount>& inBuffers, std::span<uint8> outBuffer) {
		SetStages(inBuffers, outBuffer);
	}


	bool IsConfigured() const {
		return BulkEndpointEngine::IsConfigured();
	}


	// Changes at every configuration and bus reset. See BulkEndpointEngine::GetSessionGeneration().
	uint32 GetSessionGeneration() const {
		return BulkEndpointEngine::GetSessionGeneration();
	}
};
