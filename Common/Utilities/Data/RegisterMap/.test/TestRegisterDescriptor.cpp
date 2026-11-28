#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterSet.h>
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>
#include "SimCheck.h"
#include <array>
#include <cstdio>
#include <type_traits>


// Compile-time checks: a descriptor, a bit field and a register set are constants, and each rule below holds when this file
// compiles. Expected values are hand-computed.


constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
constexpr auto ChargeStatus = RegisterDescriptor<uint8>(0x09, 0x02).ReadOnly();
constexpr auto ChargeDisable = BitField<1>(ChargeControl, 3);
constexpr auto WideField = BitField<9>(RegisterDescriptor<uint16>(0x68, 0x10), 7);
constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).AddressWidth(2).Order(ByteOrder::little).Table(RegisterTable::Input).ReadOnly();


// Defaults: address width 1, big-endian, read and write, holding table.
static_assert(ChargeControl.device == 0x09);
static_assert(ChargeControl.address == 0x01);
static_assert(ChargeControl.addressWidth == 1);
static_assert(ChargeControl.byteOrder == ByteOrder::big);
static_assert(ChargeControl.table == RegisterTable::Holding);
static_assert(ChargeControl.CanRead() && ChargeControl.CanWrite());
static_assert(ChargeControl.ByteCount() == 1);


// Each builder method changes one property and keeps the rest.
static_assert(!ChargeStatus.CanWrite() && ChargeStatus.CanRead());
static_assert(Measured.addressWidth == 2);
static_assert(Measured.byteOrder == ByteOrder::little);
static_assert(Measured.table == RegisterTable::Input);
static_assert(Measured.ByteCount() == 2);


// The bus reference carries device, address, width and table.
static_assert(Measured.Ref().device == 0x68);
static_assert(Measured.Ref().address == 0x3B);
static_assert(Measured.Ref().addressWidth == 2);
static_assert(Measured.Ref().table == RegisterTable::Input);


// Value types of bit fields: bool for one bit, otherwise the smallest unsigned type.
static_assert(std::is_same_v<decltype(ChargeDisable)::ValueType, bool>);
static_assert(std::is_same_v<decltype(WideField)::ValueType, uint16>);
static_assert(std::is_same_v<BitFieldValue<3>, uint8>);
static_assert(std::is_same_v<BitFieldValue<8>, uint8>);
static_assert(std::is_same_v<BitFieldValue<9>, uint16>);
static_assert(std::is_same_v<BitFieldValue<17>, uint32>);
static_assert(std::is_same_v<BitFieldValue<33>, uint64>);


// Bit field insert: bits 3 of 0xFF cleared is 0xF7, set on 0x00 is 0x08. A 9-bit field at bit 7 of a 16-bit register, set to 0x1FF.
static_assert(ChargeDisable.Insert(0xFF, false) == 0xF7);
static_assert(ChargeDisable.Insert(0x00, true) == 0x08);
static_assert(ChargeDisable.Fits(true));
static_assert(WideField.Insert(0x0000, 0x1FF) == 0xFF80);
static_assert(!BitField<2>(ChargeControl, 0).Fits(4));


// Big-endian and little-endian packing. 0x1234 is 12 34 big-endian and 34 12 little-endian.
constexpr std::array<uint8, 2> bigBytes = ByteConverter::ToBytes<uint16, ByteOrder::big>(0x1234);
constexpr std::array<uint8, 2> littleBytes = ByteConverter::ToBytes<uint16, ByteOrder::little>(0x1234);
static_assert(bigBytes[0] == 0x12 && bigBytes[1] == 0x34);
static_assert(littleBytes[0] == 0x34 && littleBytes[1] == 0x12);

constexpr std::array<uint8, 2> readBytes{ 0xFF, 0xFE };
static_assert(ByteConverter::FromBytes<int16, ByteOrder::big>(readBytes) == -2);
static_assert(ByteConverter::FromBytes<uint16, ByteOrder::little>(readBytes) == 0xFEFF);


// A register set with distinct registers is a constant. Two registers with one device and address are not unique.
constexpr RegisterSet<ChargeControl, ChargeStatus> distinctSet{};
static_assert(distinctSet.count == 2);
static_assert(distinctSet.unique);
static_assert(!RegisterSetIsUnique<ChargeControl, ChargeControl>());
static_assert(RegisterSetIsUnique<ChargeControl, RegisterDescriptor<uint8>(0x09, 0x02)>());
static_assert(RegisterSetIsUnique<ChargeControl, RegisterDescriptor<uint8>(0x0A, 0x01)>());

// A Modbus holding register and a coil at one address are different registers, so a set holds both.
constexpr auto ModbusHolding = RegisterDescriptor<uint8>(0x00, 0x0000);
constexpr auto ModbusCoil = RegisterDescriptor<uint8>(0x00, 0x0000).Table(RegisterTable::Coil);
static_assert(RegisterSetIsUnique<ModbusHolding, ModbusCoil>());
static_assert(!RegisterSetIsUnique<ModbusHolding, ModbusHolding>());


int main() {
	std::printf("TestRegisterDescriptor: all compile-time checks hold\n");
	return simFailures == 0 ? 0 : 1;
}
