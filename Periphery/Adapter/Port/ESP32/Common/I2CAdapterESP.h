#pragma once
#include <Adapter/I2CAdapter.h>


using AI2C = class I2CAdapterESP;


class I2CAdapterESP : public I2CAdapter<void> {
protected:
	i2c_master_bus_handle_t busHandle = nullptr;

	static constexpr uint8 maxDevices = 128;
	i2c_master_dev_handle_t devHandles[maxDevices] = {};

	i2c_port_num_t port;
	int sdaPin;
	int sclPin;


public:
	I2CAdapterESP() = default;

	I2CAdapterESP(i2c_port_num_t i2cPort, int sda, int scl)
		: port(i2cPort), sdaPin(sda), sclPin(scl) { }

	~I2CAdapterESP() override {
		for (uint8 i = 0; i < maxDevices; i++) {
			if (devHandles[i]) {
				i2c_master_bus_rm_device(devHandles[i]);
			}
		}
		if (busHandle) {
			i2c_del_master_bus(busHandle);
		}
	}


	void IrqEventHandler() override { }
	void IrqErrorHandler() override { }


	ResultStatus CheckDevice(uint8 address, uint16 repeat = 1) override {
		for (uint16 i = 0; i < repeat; i++) {
			if (i2c_master_probe(busHandle, address, timeout) != ESP_OK) {
				return ResultStatus::error;
			}
		}
		return ResultStatus::ok;
	}


	ResultStatus CheckDeviceAsync(uint8 address, uint16 repeat = 1) override {
		return ResultStatus::notSupported;
	}


	Result<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		uint8 count = 0;

		for (uint8 addr = 1; addr < 128 && count < size; addr++) {
			if (i2c_master_probe(busHandle, addr, 50) == ESP_OK) {
				listBuffer[count++] = addr;
			}
		}

		return count;
	}


	Result<uint8> ScanAsync(uint8 *listBuffer, uint8 size) override {
		return ResultStatus::notSupported;
	}


	ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		i2c_master_dev_handle_t handle;
		auto status = EnsureDeviceHandle(device, handle);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (addressSize == 0) {
			if (i2c_master_transmit(handle, writeData, dataSize, timeout) != ESP_OK) {
				return ResultStatus::error;
			}
			return ResultStatus::ok;
		}

		uint8 txBuf[2 + dataSize];
		uint32 offset = 0;

		if (addressSize == 2) {
			txBuf[offset++] = (address >> 8) & 0xFF;
		}
		txBuf[offset++] = address & 0xFF;

		memcpy(&txBuf[offset], writeData, dataSize);

		if (i2c_master_transmit(handle, txBuf, offset + dataSize, timeout) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		i2c_master_dev_handle_t handle;
		auto status = EnsureDeviceHandle(device, handle);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (addressSize == 0) {
			if (i2c_master_receive(handle, readData, dataSize, timeout) != ESP_OK) {
				return ResultStatus::error;
			}
			return ResultStatus::ok;
		}

		uint8 addrBuf[2];
		uint32 addrLen = 0;

		if (addressSize == 2) {
			addrBuf[addrLen++] = (address >> 8) & 0xFF;
		}
		addrBuf[addrLen++] = address & 0xFF;

		if (i2c_master_transmit_receive(handle, addrBuf, addrLen, readData, dataSize, timeout) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	ResultStatus WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}


	ResultStatus ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		return ResultStatus::notSupported;
	}


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (busHandle) {
			for (uint8 i = 0; i < maxDevices; i++) {
				if (devHandles[i]) {
					i2c_master_bus_rm_device(devHandles[i]);
					devHandles[i] = nullptr;
				}
			}
			i2c_del_master_bus(busHandle);
			busHandle = nullptr;
		}

		i2c_master_bus_config_t bus_config = {};
		bus_config.i2c_port = port;
		bus_config.sda_io_num = static_cast<gpio_num_t>(sdaPin);
		bus_config.scl_io_num = static_cast<gpio_num_t>(sclPin);
		bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
		bus_config.glitch_ignore_cnt = 7;
		bus_config.flags.enable_internal_pullup = !parameters.analogFilter;

		if (i2c_new_master_bus(&bus_config, &busHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


private:
	ResultStatus EnsureDeviceHandle(uint8 address, i2c_master_dev_handle_t &handle) {
		if (devHandles[address]) {
			handle = devHandles[address];
			return ResultStatus::ok;
		}

		i2c_device_config_t dev_config = {};
		dev_config.dev_addr_length = (parameters.addressingMode == AddressingMode::B10)
			? I2C_ADDR_BIT_LEN_10
			: I2C_ADDR_BIT_LEN_7;
		dev_config.device_address = address;
		dev_config.scl_speed_hz = static_cast<uint32>(parameters.speed) * 1000;

		if (i2c_master_bus_add_device(busHandle, &dev_config, &devHandles[address]) != ESP_OK) {
			return ResultStatus::error;
		}

		handle = devHandles[address];
		return ResultStatus::ok;
	}
};
