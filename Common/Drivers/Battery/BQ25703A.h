#pragma once
#include <BSP.h>
#include <functional>


class BQ25703A {
private:
	 uint16 chargeCurrentRegister;
	 bool adapterPresentStatus;

public:
	 AI2C *i2c;

	 std::function<void()> onAdapterPresent;
	 std::function<void()> onAdapterNotPresent;


public:
	 void ChargeOkPinToggle(bool pinState) {
	 	adapterPresentStatus = pinState;
	 	if(adapterPresentStatus == false) {
	 		if(onAdapterNotPresent != nullptr) { onAdapterNotPresent(); }
	 	} else {
	 		if(onAdapterPresent != nullptr) { onAdapterPresent(); }
	 	}
	 }





	 Status::info<uint16> GetChargeCurrent() {
	 	auto ret = GetChargeAndDischargeCurrentReadBack();
	 	if(ret.type != Status::ok){
	 		return ret;
	 	}
	 	ret.data = ((ret.data & 0xFF00) >> 8) * 64;

	 	return ret;
	 }





	 Status::info<uint16> GetBatteryVoltage() {
	 	auto ret = GetSystemAndBatteryVoltageReadBack();
	 	if(ret.type != Status::ok){
	 		return ret;
	 	}
	 	ret.data = 2880 + ((ret.data & 0x00FF) * 64);

	 	return ret;
	 }




	 Status::info<uint16> GetVbus() {
	 	auto ret = GetVbusAndPsysVoltageReadBack();
	 	if(ret.type != Status::ok){
	 		return ret;
	 	}
	 	ret.data = (((ret.data & 0xFF00) >> 8) * 64) + 3200;

	 	return ret;
	 }





	 Status::statusType UpdateAdc() {
	 	auto ret = GetAdcOption();
	 	if(ret.type != Status::ok){
	 		return ret.type;
	 	}

	 	return SetAdcOption(ret.data | (1 << 14));
	 }





	 Status::statusType AdcEvery1Sec(bool set) {
	 	auto ret = GetAdcOption();
	 	if(ret.type != Status::ok){
	 		return ret.type;
	 	}

	 	if(set) {
	 		return SetAdcOption(ret.data | (1 << 14) | (1 << 15));
	 	} else {
	 		return SetAdcOption(ret.data & ~(1 << 15));
	 	}
	 }





	 Status::statusType UpdateChargeCurrent() {
	 	return SetChargeCurrentRegister(chargeCurrentRegister);
	 }



	 void SetChargeCurrent(uint16 data){ chargeCurrentRegister = data; }
	 bool AdapterIsPresent() { return adapterPresentStatus; };


public:
	 Status::statusType SetChargeOption0(uint16 data){ return WriteReg(0x00, data); }
	 Status::statusType SetChargeCurrentRegister(uint16 data){
		chargeCurrentRegister = data;
		return WriteReg(0x02, chargeCurrentRegister);
	}
	 Status::statusType SetChargeVoltageRegister(uint16 data){ return WriteReg(0x04, data); }
	 Status::statusType SetOtgVoltageRegister(uint16 data){ return WriteReg(0x06, data); }
	 Status::statusType SetOtgCurrentRegister(uint16 data){ return WriteReg(0x08, data); }
	 Status::statusType SetInputVoltageRegister(uint16 data){ return WriteReg(0x0A, data); }
	 Status::statusType SetMinimumSystemVoltage(uint16 data){ return WriteReg(0x0C, data); }
	 Status::statusType SetInputCurrentRegister(uint16 data){ return WriteReg(0x0E, data); }
	 Status::statusType SetChargeOption1(uint16 data){ return WriteReg(0x30, data); }
	 Status::statusType SetChargeOption2(uint16 data){ return WriteReg(0x32, data); }
	 Status::statusType SetChargeOption3(uint16 data){ return WriteReg(0x34, data); }
	 Status::statusType SetProchotOption0(uint16 data){ return WriteReg(0x36, data); }
	 Status::statusType SetProchotOption1(uint16 data){ return WriteReg(0x38, data); }
	 Status::statusType SetAdcOption(uint16 data){ return WriteReg(0x3A, data); }

	 Status::info<uint16> GetChargeOption0(){ return ReadReg(0x00); }
	 Status::info<uint16> GetChargeCurrentRegister(){ return ReadReg(0x02); }
	 Status::info<uint16> GetChargeVoltageRegister(){ return ReadReg(0x04); }
	 Status::info<uint16> GetOtgVoltageRegister(){ return ReadReg(0x06); }
	 Status::info<uint16> GetOtgCurrentRegister(){ return ReadReg(0x08); }
	 Status::info<uint16> GetInputVoltageRegister(){ return ReadReg(0x0A); }
	 Status::info<uint16> GetMinimumSystemVoltage(){ return ReadReg(0x0C); }
	 Status::info<uint16> GetInputCurrentRegister(){ return ReadReg(0x0E); }
	 Status::info<uint16> GetChargeStatusRegister(){ return ReadReg(0x20); }
	 Status::info<uint16> GetProchotStatusRegister(){ return ReadReg(0x22); }
	 Status::info<uint16> GetInputCurrentLimitInUse(){ return ReadReg(0x24); }
	 Status::info<uint16> GetVbusAndPsysVoltageReadBack(){ return ReadReg(0x26); }
	 Status::info<uint16> GetChargeAndDischargeCurrentReadBack(){ return ReadReg(0x28); }
	 Status::info<uint16> GetInputCurrentAndCmpinVoltageReadBack(){ return ReadReg(0x2A); }
	 Status::info<uint16> GetSystemAndBatteryVoltageReadBack(){ return ReadReg(0x2C); }
	 Status::info<uint16> GetManufactureIdAndDeviceIdReadBack(){ return ReadReg(0x2E); }
	 Status::info<uint16> GetDeviceIdReadBack(){ return ReadReg(0x2F); }
	 Status::info<uint16> GetChargeOption1(){ return ReadReg(0x30); }
	 Status::info<uint16> GetChargeOption2(){ return ReadReg(0x32); }
	 Status::info<uint16> GetChargeOption3(){ return ReadReg(0x34); }
	 Status::info<uint16> GetProchotOption0(){ return ReadReg(0x36); }
	 Status::info<uint16> GetProchotOption1(){ return ReadReg(0x38); }
	 Status::info<uint16> GetAdcOption(){ return ReadReg(0x3A); }




private:
	 Status::statusType WriteReg(uint8 addrress, uint16 reg) {
		return i2c->WriteByteArray(0xd6, addrress, 1, (uint8*)&reg, 2);
	}


	 Status::info<uint16> ReadReg(uint8 addrress) {
		auto info = Status::info<uint16>();
		i2c->ReadByteArray(0xd6, addrress, 1, (uint8*)&ret.data, 2);
		return info;
	}
};



