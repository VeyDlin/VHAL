#pragma once
#include "IAdapter.h"
#include <Adapter/Utilities/NetworkTypes.h>
#include <cstring>
#include <type_traits>

#define VHAL_BLE_ADAPTER


// Bluetooth Low Energy: the peripheral role (advertising and a GATT server) and the observer role (scanning).
// Central connections and a GATT client are not part of this version.
//
// The class is not a template: a chip's built-in radio and an external BLE driver both implement it, and portable
// code takes a BLEAdapter& without knowing which one it got. All BLE types are nested here, so they do not clash
// with the names of other drivers.
//
// The GATT table is declared up front. The application creates the Service and Characteristic objects, registers them
// with AddService() before SetParameters() starts the radio, and keeps them alive while the radio runs. Once the
// radio is started the table is fixed and AddService() returns busy.
//
// Callbacks run in the context of the BLE host task, not the caller's. Do not block in them.
class BLEAdapter : public IAdapter {
public:
	using ConnectionHandle = uint16;
	static constexpr ConnectionHandle invalidConnection = 0xFFFF;


	// bytes holds the UUID in the order it is written: bytes[0] is the most significant byte.
	struct Uuid {
		static constexpr uint8 maxSize = 16;

		uint8 bytes[maxSize] = {};
		uint8 size = 0;						// 2 or 16. 0 is an empty UUID.

		constexpr Uuid() { }

		constexpr Uuid(uint16 shortUuid) {
			bytes[0] = static_cast<uint8>(shortUuid >> 8);
			bytes[1] = static_cast<uint8>(shortUuid);
			size = 2;
		}


		// Accepts 32 hex digits, dashes anywhere are ignored and letter case does not matter.
		// Anything else gives an empty UUID.
		static constexpr Uuid FromString(const char *text) {
			Uuid result;
			if (text == nullptr) {
				return result;
			}

			uint8 digits = 0;
			for (uint16 i = 0; text[i] != '\0'; i++) {
				if (text[i] == '-') {
					continue;
				}

				int8 value = HexValue(text[i]);
				if (value < 0 || digits == maxSize * 2) {
					return Uuid();
				}

				uint8 index = digits / 2;
				if ((digits % 2) == 0) {
					result.bytes[index] = static_cast<uint8>(value << 4);
				} else {
					result.bytes[index] = static_cast<uint8>(result.bytes[index] | value);
				}

				digits++;
			}

			if (digits != maxSize * 2) {
				return Uuid();
			}

			result.size = maxSize;
			return result;
		}


		constexpr bool IsValid() const {
			return size == 2 || size == maxSize;
		}


		constexpr bool operator==(const Uuid& other) const noexcept {
			if (size != other.size) {
				return false;
			}

			for (uint8 i = 0; i < maxSize; i++) {
				if (bytes[i] != other.bytes[i]) {
					return false;
				}
			}
			return true;
		}


		constexpr bool operator!=(const Uuid& other) const noexcept {
			return !(*this == other);
		}


	private:
		static constexpr int8 HexValue(char digit) {
			if (digit >= '0' && digit <= '9') {
				return static_cast<int8>(digit - '0');
			}

			if (digit >= 'a' && digit <= 'f') {
				return static_cast<int8>(digit - 'a' + 10);
			}

			if (digit >= 'A' && digit <= 'F') {
				return static_cast<int8>(digit - 'A' + 10);
			}

			return -1;
		}
	};


	enum class AddressType { Public, Random };


	// address.bytes[0] is the first octet as printed.
	struct Address {
		MacAddress address;
		AddressType type = AddressType::Public;
	};


	struct Properties {
		bool read = false;
		bool write = false;
		bool writeWithoutResponse = false;
		bool notify = false;
		bool indicate = false;
	};


	// Access requirement of a characteristic. Encrypted needs a bonded or paired link; Authenticated also needs a
	// pairing that was protected against a man in the middle.
	enum class Security { Open, Encrypted, Authenticated };


	class Characteristic {
	public:
		Uuid uuid;
		Properties properties;
		Security readSecurity = Security::Open;
		Security writeSecurity = Security::Open;

		// Runs before the value is sent, so the application can refresh the value with Set().
		std::function<void(ConnectionHandle connection)> onRead;

		// Runs after the new value is stored.
		std::function<void(ConnectionHandle connection)> onWrite;

		std::function<void(ConnectionHandle connection, bool notify, bool indicate)> onSubscribe;


		// For ports: the handle the stack assigned to the value, and the next characteristic of the service.
		uint16 valueHandle = 0;
		Characteristic *next = nullptr;


	public:
		// The value lives in the caller's buffer, which must outlive the characteristic.
		// Reads and writes of the value are not locked: the application must not call SetValue() or Set() while a
		// remote write to the same characteristic can be in progress, unless it synchronises the two itself.
		Characteristic(Uuid uuid, Properties properties, uint8 *buffer, uint16 capacity)
			: uuid(uuid), properties(properties), buffer(buffer), capacity(capacity) { }


		ResultStatus SetValue(const uint8 *data, uint16 size) {
			if (size > capacity) {
				return ResultStatus::outOfRange;
			}

			if (size > 0) {
				if (data == nullptr) {
					return ResultStatus::invalidParameter;
				}

				std::memcpy(buffer, data, size);
			}

			length = size;
			return ResultStatus::ok;
		}


		template<typename T>
		ResultStatus Set(const T& value) {
			static_assert(std::is_trivially_copyable<T>::value, "BLEAdapter: a characteristic value must be trivially copyable");
			return SetValue(reinterpret_cast<const uint8 *>(&value), sizeof(T));
		}


		const uint8 *GetValue() const {
			return buffer;
		}


		uint16 GetLength() const {
			return length;
		}


		uint16 GetCapacity() const {
			return capacity;
		}


		// invalidData when the stored length is not sizeof(T).
		template<typename T>
		Result<T> Get() const {
			static_assert(std::is_trivially_copyable<T>::value, "BLEAdapter: a characteristic value must be trivially copyable");
			if (length != sizeof(T)) {
				return ResultStatus::invalidData;
			}

			T value = {};
			std::memcpy(&value, buffer, sizeof(T));
			return value;
		}


		// For ports: the GATT access callback writes a remote write straight into the storage, then records its length.
		uint8 *GetStorage() {
			return buffer;
		}


		void SetLength(uint16 val) {
			length = val;
		}


	protected:
		uint8 *buffer;
		uint16 capacity;
		uint16 length = 0;
	};


	template<uint16 Capacity>
	class CharacteristicStatic : public Characteristic {
	protected:
		uint8 storage[Capacity] = {};


	public:
		CharacteristicStatic(Uuid uuid, Properties properties) : Characteristic(uuid, properties, storage, Capacity) { }
	};


	class Service {
	public:
		Uuid uuid;
		Characteristic *characteristics = nullptr;
		Service *next = nullptr;


	public:
		explicit Service(Uuid uuid) : uuid(uuid) { }


		// A characteristic belongs to one service. Adding one that is already in the list does nothing.
		void AddCharacteristic(Characteristic& characteristic) {
			Characteristic **link = &characteristics;
			while (*link != nullptr) {
				if (*link == &characteristic) {
					return;
				}

				link = &(*link)->next;
			}

			characteristic.next = nullptr;
			*link = &characteristic;
		}
	};


	enum class Event { Connected, Disconnected, AdvertisingStopped, ScanStopped, MtuChanged, SecurityChanged };


	enum class IoCapability { None, DisplayOnly, KeyboardOnly, DisplayYesNo, KeyboardDisplay };


	struct Parameters {
		const char *deviceName = "VHAL";
		uint16 appearance = 0;
		bool bonding = false;
		bool secureConnections = true;
		bool mitm = false;
		IoCapability ioCapability = IoCapability::None;
	};


	struct AdvertisingConfig {
		bool connectable = true;
		uint16 intervalMinMs = 100;
		uint16 intervalMaxMs = 150;
		uint32 durationMs = 0;					// 0 advertises until stopped
		bool includeName = true;
		const Uuid *serviceUuids = nullptr;
		uint8 serviceUuidCount = 0;
		const uint8 *manufacturerData = nullptr;
		uint8 manufacturerDataSize = 0;
	};


	struct ScanConfig {
		bool active = false;
		uint16 intervalMs = 100;
		uint16 windowMs = 50;
		uint32 durationMs = 0;					// 0 scans until stopped
		bool filterDuplicates = true;
	};


	// data is valid only during the callback.
	struct ScanResult {
		Address address;
		int8 rssi = 0;
		const uint8 *data = nullptr;
		uint8 dataSize = 0;
		bool connectable = false;
		bool scanResponse = false;
	};


	// Walks the length, type and value structures of advertising data and returns the first field of the given type.
	// Portable, so an application can read the device name on any port.
	static bool FindAdvertisingField(const uint8 *data, uint8 dataSize, uint8 type, const uint8 *&field, uint8 &fieldSize) {
		uint16 offset = 0;
		while (offset < dataSize) {
			uint8 length = data[offset];

			// A zero length is the padding at the end of the data.
			if (length == 0) {
				return false;
			}

			if (offset + 1 + length > dataSize) {
				return false;
			}

			if (data[offset + 1] == type) {
				field = &data[offset + 2];
				fieldSize = length - 1;
				return true;
			}

			offset += length + 1;
		}

		return false;
	}


	static constexpr uint8 fieldCompleteName = 0x09;
	static constexpr uint8 fieldShortName = 0x08;
	static constexpr uint8 fieldManufacturerData = 0xFF;


public:
	// Runs in the BLE host task. Do not block in it.
	std::function<void(Event event, ConnectionHandle connection)> onEvent;

	// Called when the stack generated a passkey that the device must show to the peer.
	std::function<void(ConnectionHandle connection, uint32 passkey)> onPasskeyDisplay;


protected:
	Parameters parameters;
	Service *services = nullptr;
	bool started = false;


public:
	// Appends the service to the table. Adding one that is already registered does nothing.
	virtual ResultStatus AddService(Service& service) {
		if (started) {
			return ResultStatus::busy;
		}

		Service **link = &services;
		while (*link != nullptr) {
			if (*link == &service) {
				return ResultStatus::ok;
			}

			link = &(*link)->next;
		}

		service.next = nullptr;
		*link = &service;
		return ResultStatus::ok;
	}


	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	inline bool IsStarted() const {
		return started;
	}


	// Turns the radio off.
	virtual ResultStatus Stop() = 0;

	virtual ResultStatus StartAdvertising(const AdvertisingConfig& config) = 0;
	virtual ResultStatus StopAdvertising() = 0;

	// Sends the current value to every connection that subscribed, as a notification or an indication according to
	// what that peer subscribed to.
	virtual ResultStatus Notify(Characteristic& characteristic) = 0;

	virtual ResultStatus Disconnect(ConnectionHandle connection) = 0;
	virtual Result<int8> GetRssi(ConnectionHandle connection) = 0;						// dBm
	virtual Result<uint16> GetMtu(ConnectionHandle connection) = 0;
	virtual Result<Address> GetAddress() = 0;											// own address
	virtual Result<Address> GetPeerAddress(ConnectionHandle connection) = 0;

	// Calls onResult for every advertising report until the scan stops.
	virtual ResultStatus StartScan(const ScanConfig& config, const std::function<void(const ScanResult& result)>& onResult) = 0;
	virtual ResultStatus StopScan() = 0;

	virtual ResultStatus SetTxPower(int8 dBm) = 0;

	// Drives radios that have no task of their own (external chips polled by the application).
	// Ports with their own task leave it empty; calling it is always safe.
	virtual void Process() { }


protected:
	virtual ResultStatus Initialization() = 0;


	inline void CallEvent(Event event, ConnectionHandle connection) {
		if (onEvent != nullptr) {
			onEvent(event, connection);
		}
	}
};
