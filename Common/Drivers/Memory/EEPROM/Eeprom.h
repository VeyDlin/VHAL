#pragma once
#include <VHAL.h>
#include <algorithm>


// A serial EEPROM on an I2C bus. A chip class (IC/) gives the geometry and the 7-bit device address, and Data<T> places a
// record in the device.
//
//   EEPROM::AT24C256C eeprom(i2c, false, false, false);   // 7-bit address 0x50
//   EEPROM::Data<Sensor> sensor(eeprom);                   // the next free place in the device
//   ResultStatus status = sensor.Set(value);
//   Result<Sensor> loaded = sensor.Get();
//
// Addresses: the I2C adapter takes 7-bit device addresses. The chip class adds its address pins to the base address. The
// adapter port builds the 8-bit control byte (for example 0xA0 for a write at address 0x50). The driver refuses a device
// address of 0x80 or more before the bus is called.
//
// Writes: a write first waits for the write cycle of an earlier write, so a chip that is still busy is not written to. A write
// that crosses a page edge is split at the edge, and after each page the driver polls the device with a probe until it
// acknowledges, or until the datasheet write cycle time has passed. A device that does not answer in that time gives timeout,
// whether it is busy or absent.
//
// Layout: Data<T> takes its address in construction order. The same records must be built in the same order every time,
// because a change of order or size moves every later record.
namespace EEPROM {
	// The geometry of a serial EEPROM, from its datasheet.
	struct Geometry {
		uint32 capacity = 0;        // Bytes in the device. The adapter takes a 16-bit address, so at most 65536.
		uint16 pageSize = 0;        // Bytes in one page. A write does not cross a page edge in one bus transaction.
		uint8 addressBytes = 0;     // Bytes in the word address, sent most significant first: 1 or 2. One byte reaches 256 bytes.
		uint32 writeCycleMs = 0;    // Datasheet maximum of one internal write cycle, in milliseconds.
	};


	template<typename DataType>
	struct Data;


	class Eeprom {
		template<typename DataType>
		friend struct Data;

	public:
		// Attempts for Data<T>::Get() and Data<T>::Set(). A failed read is repeated at once. Each write attempt waits for the write
		// cycle before it starts, and a timeout is not repeated.
		uint8 retry = 1;


	private:
		AI2C *i2c = nullptr;
		uint8 deviceAddress = 0;
		Geometry geometry;
		uint32 addressPointer = 0;


	public:
		virtual ~Eeprom() = default;


		// Two copies would hand out the same bytes to two records, so an Eeprom is not copied.
		Eeprom(const Eeprom &) = delete;


		Eeprom &operator=(const Eeprom &) = delete;


		Eeprom(AI2C &i2cBus, uint8 address7, Geometry chipGeometry):
			i2c(&i2cBus),
			deviceAddress(address7),
			geometry(chipGeometry)
		{

		}


		void SetAddress(uint8 address7) {
			deviceAddress = address7;
		}


		Geometry GetGeometry() const {
			return geometry;
		}


		ResultStatus IsMemoryReady() {
			ResultStatus usable = CheckUsable();
			if (usable != ResultStatus::ok) {
				return usable;
			}

			return i2c->CheckDevice(deviceAddress, 10);
		}


		// Reads dataOutSize bytes from memAddress. A read may run across page edges, and the chip continues to the next page.
		ResultStatus ReadMemory(uint8 *dataOut, uint32 dataOutSize, uint32 memAddress) {
			ResultStatus usable = CheckUsable();
			if (usable != ResultStatus::ok) {
				return usable;
			}

			if (dataOutSize == 0) {
				return ResultStatus::ok;
			}

			if (dataOut == nullptr) {
				return ResultStatus::invalidParameter;
			}

			if (!IsInRange(memAddress, dataOutSize)) {
				return ResultStatus::outOfRange;
			}

			return i2c->ReadByteArray(
				deviceAddress,
				static_cast<uint16>(memAddress),
				geometry.addressBytes,
				dataOut,
				dataOutSize
			);
		}


		// Writes dataSize bytes at memAddress. The write waits for an earlier write cycle, is split at page edges, and each page
		// write is followed by the wait for its own write cycle. The first failure stops the write and is returned.
		ResultStatus WriteMemory(const uint8 *data, uint32 dataSize, uint32 memAddress) {
			ResultStatus usable = CheckUsable();
			if (usable != ResultStatus::ok) {
				return usable;
			}

			if (dataSize == 0) {
				return ResultStatus::ok;
			}

			if (data == nullptr) {
				return ResultStatus::invalidParameter;
			}

			if (!IsInRange(memAddress, dataSize)) {
				return ResultStatus::outOfRange;
			}

			ResultStatus status = WaitForWriteCycle();
			if (status != ResultStatus::ok) {
				return status;
			}

			uint32 written = 0;

			while (written < dataSize) {
				uint32 address = memAddress + written;
				uint32 roomInPage = geometry.pageSize - address % geometry.pageSize;
				uint32 chunk = std::min(dataSize - written, roomInPage);

				// The port only reads the buffer, so the const is cast away for the call. A port that writes into the buffer
				// breaks this.
				status = i2c->WriteByteArray(
					deviceAddress,
					static_cast<uint16>(address),
					geometry.addressBytes,
					const_cast<uint8 *>(data + written),
					chunk
				);
				if (status != ResultStatus::ok) {
					return status;
				}

				status = WaitForWriteCycle();
				if (status != ResultStatus::ok) {
					return status;
				}

				written += chunk;
			}

			return ResultStatus::ok;
		}


	private:
		// Reserves dataSize bytes after the last reserved byte. The last byte of a record may be the last byte of the device.
		Result<uint32> GetMemoryAddress(uint32 dataSize) {
			if (dataSize > geometry.capacity - addressPointer) {
				return ResultStatus::outOfRange;
			}

			uint32 address = addressPointer;
			addressPointer += dataSize;
			return address;
		}


		// The checks made before any bus call: a chip is attached, its 7-bit address is in range, and the geometry is one the
		// adapter can address. A zero page size would divide by zero, a capacity past 65536 bytes wraps the 16-bit word address,
		// and a one-byte word address reaches 256 bytes only.
		ResultStatus CheckUsable() const {
			if (i2c == nullptr) {
				return ResultStatus::noInit;
			}

			if (deviceAddress >= 0x80) {
				return ResultStatus::badAddress;
			}

			if (geometry.capacity == 0 || geometry.capacity > 65536) {
				return ResultStatus::invalidParameter;
			}

			if (geometry.pageSize == 0) {
				return ResultStatus::invalidParameter;
			}

			if (geometry.addressBytes == 0 || geometry.addressBytes > 2) {
				return ResultStatus::invalidParameter;
			}

			if (geometry.addressBytes == 1 && geometry.capacity > 256) {
				return ResultStatus::invalidParameter;
			}

			return ResultStatus::ok;
		}


		bool IsInRange(uint32 memAddress, uint32 size) const {
			return memAddress <= geometry.capacity && size <= geometry.capacity - memAddress;
		}


		// Polls the device with a probe until it acknowledges. The last probe is made at the deadline, so a device that answers at
		// the datasheet time is not reported as a timeout. The limit is checked after each probe: a probe that blocks in the port
		// for the port's own timeout can pass the limit.
		ResultStatus WaitForWriteCycle() {
			uint64 deadline = System::GetMs() + geometry.writeCycleMs;

			while (true) {
				if (i2c->CheckDevice(deviceAddress, 1) == ResultStatus::ok) {
					return ResultStatus::ok;
				}

				if (System::GetMs() >= deadline) {
					return ResultStatus::timeout;
				}

				System::DelayMs(1);
			}
		}
	};
};
