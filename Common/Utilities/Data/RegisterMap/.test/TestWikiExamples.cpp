#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/Data/RegisterMap/RegisterSet.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <Utilities/Result/Result.h>
#include "FakeBus.h"
#include "SimCheck.h"
#include <array>
#include <cstdio>


// The snippets of the RegisterMap wiki pages, each in its own namespace, so they compile as written. A check follows each
// snippet where its result is known.


namespace DescriptorPage {
	constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
	constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B)
		.AddressWidth(2)
		.Order(ByteOrder::little)
		.Table(RegisterTable::Input)
		.ReadOnly();
	constexpr std::array<uint8, 2> packed = ByteConverter::ToBytes<uint16, ByteOrder::big>(0x1234);
	static_assert(packed[0] == 0x12);
	static_assert(Measured.Ref().addressWidth == 2);
	static_assert(ChargeControl.ByteCount() == 1);
}


namespace AccessPage {
	constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
	constexpr auto ChargeDisable = BitField<1>(ChargeControl, 3);


	void Run(FakeRegisterBus& bus) {
		bus.memory[0x01] = 0xFF;
		Result<uint8> value = Read<ChargeControl>(bus);
		SimCheck(value.IsOk());
		SimCheck(value.Value() == 0xFF);

		Write<ChargeControl>(bus, 0x00);
		SimCheck(bus.memory[0x01] == 0x00);

		bus.memory[0x01] = 0xFF;
		Modify<ChargeDisable>(bus, true);
		SimCheck(bus.memory[0x01] == 0xFF);
	}
}


namespace BitFieldPage {
	constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
	constexpr auto ChargeDisable = BitField<1>(ChargeControl, 3);
	constexpr auto Mode = BitField<2>(ChargeControl, 5);


	void Run(FakeRegisterBus& bus) {
		bus.memory[0x01] = 0x00;
		Modify<ChargeDisable>(bus, true);
		SimCheck(bus.memory[0x01] == 0x08);

		Modify<Mode>(bus, 2);
		SimCheck(bus.memory[0x01] == 0x48);
	}
}


namespace SetPage {
	constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
	constexpr auto ChargeStatus = RegisterDescriptor<uint8>(0x09, 0x02).ReadOnly();


	constexpr RegisterSet<ChargeControl, ChargeStatus> board{};
	static_assert(board.count == 2);
}


namespace ViewPage {
	uint16 setpoint = 0;
	bool relayState = false;


	bool CheckSetpoint(uint16 value) {
		return value <= 100;
	}


	uint16 ReadChipTemperature() {
		return 215;
	}


	using Table = RegisterView<+[] {
		return RegisterViewBuilder()
			.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
			.InputRegisters(0x0010, Computed(&ReadChipTemperature))
			.Coils(0x0000, Variable(&relayState));
	}>;


	static_assert(Table::checked);


	void Run() {
		std::array<uint16, 1> words{};
		ResultStatus status = Table::ReadWords(RegisterTable::Holding, 0x0000, words);
		SimCheck(status == ResultStatus::ok);

		std::array<uint16, 1> newValue{ 42 };
		status = Table::WriteWords(RegisterTable::Holding, 0x0000, newValue);
		SimCheck(status == ResultStatus::ok);
		SimCheck(setpoint == 42);

		std::array<bool, 1> coils{ true };
		status = Table::WriteBits(RegisterTable::Coil, 0x0000, coils);
		SimCheck(status == ResultStatus::ok);
		SimCheck(relayState);

		std::array<bool, 1> readBack{ false };
		status = Table::ReadBits(RegisterTable::Coil, 0x0000, readBack);
		SimCheck(status == ResultStatus::ok);
		SimCheck(readBack[0]);
	}
}


namespace BusPage {
	static_assert(RegisterBus<FakeRegisterBus>);
}


int main() {
	FakeRegisterBus bus;
	AccessPage::Run(bus);
	BitFieldPage::Run(bus);
	ViewPage::Run();

	if (simFailures != 0) {
		std::printf("TestWikiExamples: %d check(s) failed\n", simFailures);
		return 1;
	}

	std::printf("TestWikiExamples: every snippet compiled and checked\n");
	return 0;
}
