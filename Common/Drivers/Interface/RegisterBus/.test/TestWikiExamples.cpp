#include <Drivers/Interface/RegisterBus/I2CRegisterBus.h>
#include <Drivers/Interface/RegisterBus/SpiRegisterBus.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <Utilities/Result/Result.h>
#include "FakeBus.h"
#include "FakeSpi.h"
#include "SimCheck.h"
#include <cstdio>


// The snippets of the register bus wiki pages, each in its own namespace. The port adapters are stood in for by the fakes,
// under the names the wiki uses.


using I2CAdapterG4 = FakeI2cAdapter;
using SPIAdapterG4 = FakeSpiAdapter;


I2CAdapterG4 i2c;
SPIAdapterG4 spi;


namespace ReadmeExample {
	void Run() {
		constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();

		i2c.memory[0x3B] = 0x12;
		i2c.memory[0x3C] = 0x34;
		I2CRegisterBus<I2CAdapterG4> bus(i2c);
		Result<uint16> value = Read<Measured>(bus);
		SimCheck(value.IsOk());
		SimCheck(value.Value() == 0x1234);
	}
}


namespace I2cPage {
	void Run() {
		constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();
		constexpr auto Wide = RegisterDescriptor<uint8>(0x50, 0x0102).AddressWidth(2);

		I2CRegisterBus<I2CAdapterG4> bus(i2c);

		Result<uint16> value = Read<Measured>(bus);
		SimCheck(value.IsOk());
		SimCheck(Write<Wide>(bus, 0x7E) == ResultStatus::ok);
		SimCheck(i2c.memory[0x0102] == 0x7E);
	}
}


namespace SpiPage {
	using Bus = SpiRegisterBus<SPIAdapterG4, +[] {
		return SpiFramingBuilder().ReadBit(7).AddressBits(7);
	}>;


	void Run() {
		constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();

		spi.reply = { 0x12, 0x34 };
		Bus bus(spi);
		Result<uint16> value = Read<Measured>(bus);
		SimCheck(value.IsOk());
		SimCheck(value.Value() == 0x1234);
	}
}


namespace SpiFramingPage {
	using SixBits = SpiRegisterBus<SPIAdapterG4, +[] {
		return SpiFramingBuilder().ReadBit(7).AddressBits(6);
	}>;


	void Run() {
		constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();

		FakeSpiAdapter adapter;
		adapter.reply = { 0x00, 0x00 };
		SixBits bus(adapter);
		Result<uint16> value = Read<Measured>(bus);
		SimCheck(value.IsOk());
		SimCheck(adapter.events[1].bytes[0] == 0xBB);
	}
}


int main() {
	ReadmeExample::Run();
	I2cPage::Run();
	SpiPage::Run();
	SpiFramingPage::Run();

	if (simFailures != 0) {
		std::printf("TestWikiExamples (bus): %d check(s) failed\n", simFailures);
		return 1;
	}

	std::printf("TestWikiExamples (bus): every snippet compiled and checked\n");
	return 0;
}
