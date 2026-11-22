#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>


// The register bus over an I2C adapter, for the free templates of RegisterAccess.h. Adapter is an I2CAdapter<...> of the port,
// or any class with the same ReadArray and WriteArray. The bus keeps a reference to the adapter; the adapter must outlive it.
// The descriptor's address width sets the register address size on the wire: 1 byte or 2 bytes, most significant first.
// A 1-byte address above 0xFF, and any other width, return invalidParameter and nothing is sent.
//
//   I2CRegisterBus<I2CAdapterG4> bus(i2c);
//   Result<uint8> value = Read<ChargeControl>(bus);
template<typename Adapter>
class I2CRegisterBus {
private:
	Adapter& adapter;


public:
	explicit I2CRegisterBus(Adapter& i2c) : adapter(i2c) {
	}


	ResultStatus ReadRegs(RegisterRef where, std::span<uint8> in) {
		if (where.addressWidth == 1 && where.address <= 0xFF) {
			return adapter.ReadArray(where.device, static_cast<uint8>(where.address), in.data(), static_cast<uint32>(in.size()));
		}
		if (where.addressWidth == 2) {
			return adapter.ReadArray(where.device, where.address, in.data(), static_cast<uint32>(in.size()));
		}
		return ResultStatus::invalidParameter;
	}


	ResultStatus WriteRegs(RegisterRef where, std::span<const uint8> out) {
		if (where.addressWidth == 1 && where.address <= 0xFF) {
			return adapter.WriteArray(where.device, static_cast<uint8>(where.address), out.data(), static_cast<uint32>(out.size()));
		}
		if (where.addressWidth == 2) {
			return adapter.WriteArray(where.device, where.address, out.data(), static_cast<uint32>(out.size()));
		}
		return ResultStatus::invalidParameter;
	}
};
