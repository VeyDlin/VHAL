#include "SimCheck.h"
#include "ScriptedLink.h"
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/ModbusRegisterBus.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <Utilities/Data/RegisterMap/RegisterBus.h>
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <vector>


// The register bus over a client: each register's table picks the function code, and the free templates of the register map read
// and write through it. The expected frames are hand-worked and sealed with the library CRC.


using Plc = ModbusClient<ScriptedLink, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(20)
		.Retries(0);
}>;


using Bus = ModbusRegisterBus<Plc>;


static_assert(RegisterBus<Bus>, "the Modbus register bus meets the RegisterBus concept");


constexpr auto Setpoint = RegisterDescriptor<uint16>(0, 0x0000);
constexpr auto Sensor = RegisterDescriptor<uint16>(0, 0x0010).Table(RegisterTable::Input).ReadOnly();
constexpr auto Relay = RegisterDescriptor<uint8>(0, 0x0005).Table(RegisterTable::Coil);
constexpr auto Alarm = RegisterDescriptor<uint8>(0, 0x0006).Table(RegisterTable::Discrete).ReadOnly();
constexpr auto Wide = RegisterDescriptor<uint32>(0, 0x0020);
constexpr auto Control = RegisterDescriptor<uint16>(0, 0x0000);
constexpr auto Disable = BitField<1>(Control, 3);


void TestHoldingReadUsesFunction03() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x03, 0x02, 0x12, 0x34}));

	Result<uint16> value = Read<Setpoint>(bus);
	SIM_EXPECT(value.IsOk() && value.Value() == 0x1234);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x01})));
}


void TestHoldingWriteOneRegisterUsesFunction06() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x06, 0x00, 0x00, 0x01, 0x02}));

	SIM_EXPECT(Write<Setpoint>(bus, 0x0102) == ResultStatus::ok);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x06, 0x00, 0x00, 0x01, 0x02})));
}


// A 32-bit value is two registers, most significant first: function 10 with quantity 2.
void TestWideWriteUsesFunction10() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x10, 0x00, 0x20, 0x00, 0x02}));

	SIM_EXPECT(Write<Wide>(bus, 0x11223344) == ResultStatus::ok);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x10, 0x00, 0x20, 0x00, 0x02, 0x04, 0x11, 0x22, 0x33, 0x44})));
}


void TestWideReadUsesFunction03WithQuantityTwo() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02}));

	Result<uint32> value = Read<Wide>(bus);
	SIM_EXPECT(value.IsOk() && value.Value() == 0x00010002);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x03, 0x00, 0x20, 0x00, 0x02})));
}


void TestInputReadUsesFunction04() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x04, 0x02, 0x00, 0x42}));

	Result<uint16> value = Read<Sensor>(bus);
	SIM_EXPECT(value.IsOk() && value.Value() == 0x0042);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x04, 0x00, 0x10, 0x00, 0x01})));
}


void TestCoilUsesFunctions01And05() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x01, 0x01, 0x01}));
	link.PushFrame(ScriptedSeal({0x01, 0x05, 0x00, 0x05, 0xFF, 0x00}));
	link.PushFrame(ScriptedSeal({0x01, 0x05, 0x00, 0x05, 0x00, 0x00}));

	Result<uint8> value = Read<Relay>(bus);
	SIM_EXPECT(value.IsOk() && value.Value() == 1);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x01, 0x00, 0x05, 0x00, 0x01})));

	SIM_EXPECT(Write<Relay>(bus, 1) == ResultStatus::ok);
	SIM_EXPECT((link.Sent()[1] == ScriptedSeal({0x01, 0x05, 0x00, 0x05, 0xFF, 0x00})));

	SIM_EXPECT(Write<Relay>(bus, 0) == ResultStatus::ok);
	SIM_EXPECT((link.Sent()[2] == ScriptedSeal({0x01, 0x05, 0x00, 0x05, 0x00, 0x00})));
}


void TestDiscreteReadUsesFunction02() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x02, 0x01, 0x01}));

	Result<uint8> value = Read<Alarm>(bus);
	SIM_EXPECT(value.IsOk() && value.Value() == 1);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x02, 0x00, 0x06, 0x00, 0x01})));
}


// Modify reads the register, changes bit 3, and writes it back: one read and one write, the other bits kept.
void TestModifyIsOneReadAndOneWrite() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	link.PushFrame(ScriptedSeal({0x01, 0x03, 0x02, 0x00, 0x05}));
	link.PushFrame(ScriptedSeal({0x01, 0x06, 0x00, 0x00, 0x00, 0x0D}));

	SIM_EXPECT(Modify<Disable>(bus, true) == ResultStatus::ok);
	SIM_EXPECT(link.Sent().size() == 2);
	SIM_EXPECT((link.Sent()[0] == ScriptedSeal({0x01, 0x03, 0x00, 0x00, 0x00, 0x01})));
	SIM_EXPECT((link.Sent()[1] == ScriptedSeal({0x01, 0x06, 0x00, 0x00, 0x00, 0x0D})));
}


// A value that does not fit the field is refused before any bus traffic.
void TestBusRejectsWrongShapeWithoutTraffic() {
	ScriptedLink link;
	Plc plc(link);
	Bus bus(plc);
	std::vector<uint8> three = {0x00, 0x01, 0x02};

	SIM_EXPECT(bus.ReadRegs(Setpoint.Ref(), std::span<uint8>(three)) == ResultStatus::invalidParameter);
	SIM_EXPECT(bus.WriteRegs(Relay.Ref(), std::span<const uint8>(three)) == ResultStatus::invalidParameter);
	SIM_EXPECT(bus.WriteRegs(Sensor.Ref(), std::span<const uint8>(three)) == ResultStatus::notSupported);
	SIM_EXPECT(link.Sent().empty());
}


int main() {
	TestHoldingReadUsesFunction03();
	TestHoldingWriteOneRegisterUsesFunction06();
	TestWideWriteUsesFunction10();
	TestWideReadUsesFunction03WithQuantityTwo();
	TestInputReadUsesFunction04();
	TestCoilUsesFunctions01And05();
	TestDiscreteReadUsesFunction02();
	TestModifyIsOneReadAndOneWrite();
	TestBusRejectsWrongShapeWithoutTraffic();
	return SimFinish("TestModbusRegisterBus");
}
