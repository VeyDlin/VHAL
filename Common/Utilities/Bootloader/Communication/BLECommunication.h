#pragma once
#include "../ICommunication.h"
#include <Adapter/BLEAdapter.h>
#include <Drivers/Interface/User/StreamingProtocol/CompactStreamingProtocol.h>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <span>


// Bootloader transport over the generic BLE adapter. Wire-compatible with NRF8001Communication: the same service and
// characteristics, CompactStreamingProtocol framing with 20-byte packets, and the same reset signals to the bootloader.
//
// Setup order: RegisterService() (or Initialize()) -> ble.SetParameters() -> StartAdvertising() -> Process() regularly.
// RegisterService() must run before SetParameters(), because the GATT table is fixed once the radio starts.
//
// The constructor takes over ble.onEvent. An application that needs the events sets the onEvent member of this class
// instead, which runs after this class has handled the event.
//
// Threading: onWrite and onEvent run in the BLE host task. They copy packets into a fixed queue, and onEvent empties
// that queue on connect and disconnect. Process(), SendData() and the data callback run in the application's task,
// and the protocol is used from that task only.
template<size_t MaxPacketCount = 4, size_t RxQueuePackets = 16>
class BLECommunication : public ICommunication {
	static_assert(RxQueuePackets > 0, "BLECommunication needs at least one receive packet");

public:
	static constexpr size_t packetSize = 20;

	// Runs in the BLE host task after this class has handled the event. Do not block in it.
	std::function<void(BLEAdapter::Event event, BLEAdapter::ConnectionHandle connection)> onEvent;


private:
	using Protocol = CompactStreamingProtocol<packetSize, MaxPacketCount>;

	struct RxPacket {
		std::array<uint8, packetSize> data = {};
		size_t length = 0;
	};

	static constexpr const char *serviceUuidText = "00004242-0000-1000-8000-00805F9B34FB";
	static constexpr const char *rxUuidText = "00004243-0000-1000-8000-00805F9B34FB";
	static constexpr const char *txUuidText = "00004244-0000-1000-8000-00805F9B34FB";

	BLEAdapter& ble;

	// The adapter keeps pointers to these objects, so they must not move and must outlive the radio.
	BLEAdapter::Uuid serviceUuid = BLEAdapter::Uuid::FromString(serviceUuidText);
	BLEAdapter::Service service;
	BLEAdapter::CharacteristicStatic<packetSize> rxCharacteristic;
	BLEAdapter::CharacteristicStatic<packetSize> txCharacteristic;

	// Filled by the host task in onWrite, drained by Process(). Guarded by a critical section for the copy only.
	std::array<RxPacket, RxQueuePackets> rxQueue = {};
	size_t rxHead = 0;
	size_t rxCount = 0;
	uint32 droppedPackets = 0;

	// Set by the host task in onEvent.
	std::atomic<bool> linkUp = false;

	// Application task only.
	bool connected = false;
	bool advertising = false;
	BLEAdapter::AdvertisingConfig advertisingConfig;
	DataReceivedCallback dataCallback;
	Protocol protocol;


public:
	BLECommunication(BLEAdapter& bleAdapter) :
		ble(bleAdapter),
		service(serviceUuid),
		rxCharacteristic(BLEAdapter::Uuid::FromString(rxUuidText), { .write = true, .writeWithoutResponse = true }),
		txCharacteristic(BLEAdapter::Uuid::FromString(txUuidText), { .notify = true }),
		protocol(
			// Transmit callback: one packet as a notification
			[this](std::span<const uint8> data) -> bool {
				return SendPacket(data);
			},
			// Data received callback: passed on to the bootloader
			[this](uint8, std::span<const uint8> data) -> bool {
				return HandleReceivedData(data);
			},
			// No stream completion handler: a TX timeout must not reset the bootloader
			nullptr
		) {
		rxCharacteristic.onWrite = [this](BLEAdapter::ConnectionHandle) {
			OnRxWritten();
		};

		service.AddCharacteristic(rxCharacteristic);
		service.AddCharacteristic(txCharacteristic);

		ble.onEvent = [this](BLEAdapter::Event event, BLEAdapter::ConnectionHandle connection) {
			OnBleEvent(event, connection);
		};
	}

	~BLECommunication() override {
		ble.onEvent = nullptr;
	}

	BLECommunication(const BLECommunication&) = delete;
	BLECommunication& operator=(const BLECommunication&) = delete;


	// Adds the service to the adapter's GATT table. Call it before ble.SetParameters() starts the radio.
	// Returns busy when the radio is already started. Adding the service again does nothing.
	ResultStatus RegisterService() {
		return ble.AddService(service);
	}


	ResultStatus Initialize() override {
		return RegisterService();
	}


	// Advertises the bootloader service UUID unless the caller gives service UUIDs. The config's manufacturer data must
	// outlive advertising, as it does for the adapter. The advertising is restarted after each disconnect.
	ResultStatus StartAdvertising(const BLEAdapter::AdvertisingConfig& config) {
		advertisingConfig = config;
		if (advertisingConfig.serviceUuidCount == 0) {
			advertisingConfig.serviceUuids = &serviceUuid;
			advertisingConfig.serviceUuidCount = 1;
		}

		ResultStatus status = ble.StartAdvertising(advertisingConfig);
		advertising = status == ResultStatus::ok;
		return status;
	}


	ResultStatus StopAdvertising() {
		advertising = false;
		return ble.StopAdvertising();
	}


	ResultStatus SendData(std::span<const uint8> data) override {
		if (!connected) {
			return ResultStatus::notReady;
		}

		typename Protocol::SendResult result = protocol.Send(data);

		if (result.status == Protocol::SendStatus::Success) {
			return ResultStatus::ok;
		}

		if (result.status == Protocol::SendStatus::Busy) {
			return ResultStatus::busy;
		}

		return ResultStatus::error;
	}


	void SetDataReceivedCallback(DataReceivedCallback callback) override {
		dataCallback = callback;
	}


	bool IsConnected() const override {
		return connected;
	}


	bool IsReadyToSend() const override {
		// Ready only when connected and the protocol is not sending a message, as in NRF8001Communication.
		return connected && !protocol.IsTransmitting();
	}


	size_t GetMaxPacketSize() const override {
		return packetSize;
	}


	// Packets the host task could not queue because the queue was full. The protocol's retries recover them.
	uint32 GetDroppedPackets() const {
		System::CriticalSection(true);
		uint32 dropped = droppedPackets;
		System::CriticalSection(false);
		return dropped;
	}


	void Process() override {
		bool linkUpNow = linkUp;
		if (linkUpNow && !connected) {
			connected = true;
			OnConnected();
		} else if (!linkUpNow && connected) {
			connected = false;
			OnDisconnected();
		}

		if (!connected) {
			return;
		}

		DrainReceivedPackets();
		protocol.Process();
	}


private:
	// Host task. Copies the value out of the adapter, so the adapter's storage is free again when the callback returns.
	void OnRxWritten() {
		uint16 length = rxCharacteristic.GetLength();
		const uint8 *value = rxCharacteristic.GetValue();

		System::CriticalSection(true);
		if (length > packetSize || rxCount >= RxQueuePackets) {
			droppedPackets++;
		} else {
			RxPacket& slot = rxQueue[(rxHead + rxCount) % RxQueuePackets];
			std::memcpy(slot.data.data(), value, length);
			slot.length = length;
			rxCount++;
		}
		System::CriticalSection(false);
	}


	// Host task. The queue is emptied before the link flag changes, so a packet written on the old link cannot reach
	// the protocol of the next one, even if the application task has not run Process() yet.
	void OnBleEvent(BLEAdapter::Event event, BLEAdapter::ConnectionHandle connection) {
		if (event == BLEAdapter::Event::Connected) {
			ClearReceivedPackets();
			linkUp = true;
		} else if (event == BLEAdapter::Event::Disconnected) {
			ClearReceivedPackets();
			linkUp = false;
		}

		if (onEvent != nullptr) {
			onEvent(event, connection);
		}
	}


	void ClearReceivedPackets() {
		System::CriticalSection(true);
		rxHead = 0;
		rxCount = 0;
		System::CriticalSection(false);
	}


	// Application task.
	bool DequeueReceivedPacket(RxPacket& packet) {
		System::CriticalSection(true);
		bool dequeued = rxCount > 0;
		if (dequeued) {
			packet = rxQueue[rxHead];
			rxHead = (rxHead + 1) % RxQueuePackets;
			rxCount--;
		}
		System::CriticalSection(false);
		return dequeued;
	}


	void DrainReceivedPackets() {
		RxPacket packet;
		while (DequeueReceivedPacket(packet)) {
			if (packet.length > 0) {
				protocol.DataReceived(std::span<const uint8>(packet.data.data(), packet.length));
			}
		}
	}


	// Transport lost framing: reset the protocol and tell the bootloader to drop partial input
	void ResetTransport() {
		protocol.Reset();

		if (dataCallback != nullptr) {
			dataCallback(std::span<const uint8>());
		}
	}


	void OnConnected() {
		ResetTransport();
	}


	void OnDisconnected() {
		ResetTransport();

		// The NRF8001 driver restarts advertising after a disconnect. The ESP32 port does not, so this class does it.
		if (advertising) {
			ble.StartAdvertising(advertisingConfig);
		}
	}


	// Transmit callback of the protocol. Runs in the application task.
	bool SendPacket(std::span<const uint8> data) {
		if (!connected) {
			return false;
		}

		if (data.size() > packetSize) {
			return false;
		}

		if (txCharacteristic.SetValue(data.data(), static_cast<uint16>(data.size())) != ResultStatus::ok) {
			return false;
		}

		return ble.Notify(txCharacteristic) == ResultStatus::ok;
	}


	bool HandleReceivedData(std::span<const uint8> data) {
		// Empty fragments are ACKed by the protocol and carry no data
		if (data.empty()) {
			return true;
		}

		if (dataCallback != nullptr) {
			dataCallback(data);
		}

		return true;
	}
};
