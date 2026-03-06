#pragma once
#include <VHAL.h>
#include "BQ25798Registers.h"


using Address = BQ25798Registers::Address;
using Shift = BQ25798Registers::Shift;
using Mask = BQ25798Registers::Mask;
using ConfigConstants = BQ25798Registers::Config;
using DeviceInfo = BQ25798Registers::DeviceInfo;

class BQ25798 {
public:
    enum class ChargingStatus : uint8 {
        NotCharging = 0,
        TrickleCharge = 1,
        PreCharge = 2,
        FastChargeCC = 3,
        TaperChargeCV = 4,
        Reserved = 5,
        TopOff = 6,
        Done = 7
    };

    enum class CellCount : uint8 {
        S1 = 0, S2 = 1, S3 = 2, S4 = 3
    };

    enum class WatchdogTimer : uint8 {
        Disabled = 0,
        S0_5 = 1, S1 = 2, S2 = 3,
        S20 = 4, S40 = 5, S80 = 6, S160 = 7
    };

    enum class FastChargeTimer : uint8 {
        H5 = 0, H8 = 1, H12 = 2, H24 = 3
    };

    enum class VacOvp : uint8 {
        V26 = 0, V22 = 1, V12 = 2, V7 = 3
    };

    enum class ShipMode : uint8 {
        Idle = 0, Shutdown = 1, Ship = 2, SystemReset = 3
    };

    enum class VbatLowV : uint8 {
        Pct15 = 0, Pct62 = 1, Pct67 = 2, Pct71 = 3
    };

    struct FaultStatus {
        bool vbusOvp;
        bool vbatOvp;
        bool ibusOcp;
        bool ibatOcp;
        bool convOcp;
        bool vac1Ovp;
        bool vac2Ovp;
        bool vsysOvp;
        bool vsysShort;
        bool otgUvp;
        bool otgOvp;
        bool thermalShutdown;
    };

    struct PowerStatus {
        bool vbusPresent;
        bool powerGood;
        bool vindpmActive;
        bool iindpmActive;
        bool watchdogFault;
    };

    struct VoltageMeasurement {
        uint16 mv = 0;
        float v() const { return mv / 1000.0f; }
    };

    struct CurrentMeasurement {
        int16 ma = 0;
        float a() const { return ma / 1000.0f; }
    };

    struct TemperatureMeasurement {
        int16 raw = 0; // 0.5°C resolution
        float c() const { return raw * 0.5f; }
    };

    struct AdcResult {
        CurrentMeasurement ibus;
        CurrentMeasurement ibat;
        VoltageMeasurement vbus;
        VoltageMeasurement vac1;
        VoltageMeasurement vac2;
        VoltageMeasurement vbat;
        VoltageMeasurement vsys;
        TemperatureMeasurement tdie;
    };


private:
    AI2C *i2c = nullptr;


public:
    BQ25798() { }
    BQ25798(AI2C& _i2c): i2c(&_i2c) { }



    // ========== Charging control ==========

    Status::statusType EnableCharging(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnChg, enable ? Mask::EnChg : 0);
    }


    Status::info<bool> IsChargingEnabled() {
        return CheckBitRegister(Address::ChargerCtrl0, Mask::EnChg);
    }


    Status::statusType SetChargeVoltage(float voltage) {
        uint16 regValue = ConfigConstants::ChargeVoltage.Encode(voltage);
        return WriteRegister16(Address::ChargeVoltageLimit, regValue);
    }


    Status::info<float> GetChargeVoltage() {
        auto read = ReadRegister16(Address::ChargeVoltageLimit);
        StatusAssert(read.type);
        return ConfigConstants::ChargeVoltage.Decode(read.data & 0x07FF);
    }


    Status::statusType SetChargeCurrent(float current) {
        uint16 regValue = ConfigConstants::ChargeCurrent.Encode(current);
        return WriteRegister16(Address::ChargeCurrentLimit, regValue);
    }


    Status::info<float> GetChargeCurrent() {
        auto read = ReadRegister16(Address::ChargeCurrentLimit);
        StatusAssert(read.type);
        return ConfigConstants::ChargeCurrent.Decode(read.data & 0x01FF);
    }


    Status::statusType SetTerminationCurrent(float current) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::TermCurrent.Encode(current));
        return UpdateRegisterBits(Address::TerminationControl, Mask::Iterm, regValue << Shift::Iterm);
    }


    Status::statusType EnableTermination(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnTerm, enable ? Mask::EnTerm : 0);
    }


    Status::statusType SetPrechargeCurrent(float current) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::PrechargeCurrent.Encode(current));
        return UpdateRegisterBits(Address::PrechargeControl, Mask::Iprechg, regValue << Shift::Iprechg);
    }


    Status::statusType SetPrechargeThreshold(VbatLowV threshold) {
        return UpdateRegisterBits(Address::PrechargeControl, Mask::VbatLowV, static_cast<uint8>(threshold) << Shift::VbatLowV);
    }


    Status::statusType SetRechargeOffset(float voltage) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::RechargeOffset.Encode(voltage));
        return UpdateRegisterBits(Address::RechargeControl, Mask::Vrechg, regValue << Shift::Vrechg);
    }


    Status::statusType SetCellCount(CellCount cells) {
        return UpdateRegisterBits(Address::RechargeControl, Mask::CellCount, static_cast<uint8>(cells) << Shift::CellCount);
    }



    // ========== Input limits ==========

    Status::statusType SetInputVoltageLimit(float voltage) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::InputVoltage.Encode(voltage));
        return WriteRegister(Address::InputVoltageLimit, regValue);
    }


    Status::statusType SetInputCurrentLimit(float current) {
        uint16 regValue = ConfigConstants::InputCurrent.Encode(current);
        return WriteRegister16(Address::InputCurrentLimit, regValue);
    }


    Status::info<float> GetInputCurrentLimit() {
        auto read = ReadRegister16(Address::InputCurrentLimit);
        StatusAssert(read.type);
        return ConfigConstants::InputCurrent.Decode(read.data & 0x01FF);
    }


    Status::statusType SetMinSystemVoltage(float voltage) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::MinSystemV.Encode(voltage));
        return UpdateRegisterBits(Address::MinSystemVoltage, Mask::MinSysV, regValue);
    }


    Status::statusType SetVacOvp(VacOvp threshold) {
        return UpdateRegisterBits(Address::ChargerCtrl1, Mask::VacOvp, static_cast<uint8>(threshold) << Shift::VacOvp);
    }



    // ========== Power path ==========

    Status::statusType SetHighImpedanceMode(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnHiz, enable ? Mask::EnHiz : 0);
    }


    Status::info<bool> IsInHighImpedanceMode() {
        return CheckBitRegister(Address::ChargerCtrl0, Mask::EnHiz);
    }



    // ========== OTG mode ==========

    Status::statusType EnableOtg(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl3, Mask::EnOtg, enable ? Mask::EnOtg : 0);
    }


    Status::statusType SetOtgVoltage(float voltage) {
        uint16 regValue = ConfigConstants::OtgVoltage.Encode(voltage);
        return WriteRegister16(Address::OtgVoltage, regValue);
    }


    Status::statusType SetOtgCurrentLimit(float current) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::OtgCurrent.Encode(current));
        return UpdateRegisterBits(Address::OtgRegulation, Mask::Iotg, regValue << Shift::Iotg);
    }



    // ========== Timers & watchdog ==========

    Status::statusType SetWatchdog(WatchdogTimer timer) {
        return UpdateRegisterBits(Address::ChargerCtrl1, Mask::Watchdog, static_cast<uint8>(timer) << Shift::Watchdog);
    }


    Status::statusType KickWatchdog() {
        return UpdateRegisterBits(Address::ChargerCtrl1, Mask::WdRst, Mask::WdRst);
    }


    Status::statusType EnableSafetyTimer(bool enable) {
        return UpdateRegisterBits(Address::TimerControl, Mask::EnChgTmr, enable ? Mask::EnChgTmr : 0);
    }


    Status::statusType SetSafetyTimer(FastChargeTimer timer) {
        return UpdateRegisterBits(Address::TimerControl, Mask::ChgTmr, static_cast<uint8>(timer) << Shift::ChgTmr);
    }



    // ========== Special modes ==========

    Status::statusType SetShipMode(ShipMode mode) {
        return UpdateRegisterBits(Address::ChargerCtrl2, Mask::SdrvCtrl, static_cast<uint8>(mode) << Shift::SdrvCtrl);
    }


    Status::statusType EnableBackupMode(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnBackup, enable ? Mask::EnBackup : 0);
    }


    Status::statusType EnableIco(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnIco, enable ? Mask::EnIco : 0);
    }


    Status::statusType Reset() {
        return UpdateRegisterBits(Address::TerminationControl, Mask::RegRst, Mask::RegRst);
    }



    // ========== ADC ==========

    Status::statusType EnableAdc(bool enable, bool continuous = true) {
        uint8 val = 0;
        if (enable) {
            val |= Mask::AdcEn;
            if (!continuous) {
                val |= Mask::AdcRate;
            }
        }
        return UpdateRegisterBits(Address::AdcControl, Mask::AdcEn | Mask::AdcRate, val);
    }


    Status::info<AdcResult> ReadAdc() {
        AdcResult result{};

        auto ibus = ReadRegister16(Address::IbusAdc);
        StatusAssert(ibus.type);
        result.ibus.ma = static_cast<int16>(ibus.data);

        auto ibat = ReadRegister16(Address::IbatAdc);
        StatusAssert(ibat.type);
        result.ibat.ma = static_cast<int16>(ibat.data);

        auto vbus = ReadRegister16(Address::VbusAdc);
        StatusAssert(vbus.type);
        result.vbus.mv = vbus.data;

        auto vac1 = ReadRegister16(Address::Vac1Adc);
        StatusAssert(vac1.type);
        result.vac1.mv = vac1.data;

        auto vac2 = ReadRegister16(Address::Vac2Adc);
        StatusAssert(vac2.type);
        result.vac2.mv = vac2.data;

        auto vbat = ReadRegister16(Address::VbatAdc);
        StatusAssert(vbat.type);
        result.vbat.mv = vbat.data;

        auto vsys = ReadRegister16(Address::VsysAdc);
        StatusAssert(vsys.type);
        result.vsys.mv = vsys.data;

        auto tdie = ReadRegister16(Address::TdieAdc);
        StatusAssert(tdie.type);
        result.tdie.raw = static_cast<int16>(tdie.data);

        return result;
    }


    Status::info<VoltageMeasurement> ReadVbus() {
        auto read = ReadRegister16(Address::VbusAdc);
        StatusAssert(read.type);
        return VoltageMeasurement{ read.data };
    }


    Status::info<VoltageMeasurement> ReadVbat() {
        auto read = ReadRegister16(Address::VbatAdc);
        StatusAssert(read.type);
        return VoltageMeasurement{ read.data };
    }


    Status::info<CurrentMeasurement> ReadIbat() {
        auto read = ReadRegister16(Address::IbatAdc);
        StatusAssert(read.type);
        return CurrentMeasurement{ static_cast<int16>(read.data) };
    }



    // ========== Status and faults ==========

    Status::info<ChargingStatus> GetChargingStatus() {
        auto read = ReadRegister(Address::ChargerStatus1);
        StatusAssert(read.type);
        return static_cast<ChargingStatus>((read.data & Mask::ChgStat) >> Shift::ChgStat);
    }


    Status::info<PowerStatus> GetPowerStatus() {
        auto read = ReadRegister(Address::ChargerStatus0);
        StatusAssert(read.type);

        PowerStatus status{};
        status.vbusPresent = read.data & Mask::VbusPresent;
        status.powerGood = read.data & Mask::PgStat;
        status.vindpmActive = read.data & Mask::VindpmStat;
        status.iindpmActive = read.data & Mask::IindpmStat;
        status.watchdogFault = read.data & Mask::WdStat;
        return status;
    }


    Status::info<FaultStatus> GetFaultStatus() {
        auto f0 = ReadRegister(Address::FaultStatus0);
        StatusAssert(f0.type);

        auto f1 = ReadRegister(Address::FaultStatus1);
        StatusAssert(f1.type);

        FaultStatus status{};
        status.vbusOvp = f0.data & Mask::VbusOvp;
        status.vbatOvp = f0.data & Mask::VbatOvp;
        status.ibusOcp = f0.data & Mask::IbusOcp;
        status.ibatOcp = f0.data & Mask::IbatOcp;
        status.convOcp = f0.data & Mask::ConvOcp;
        status.vac1Ovp = f0.data & Mask::Vac1Ovp;
        status.vac2Ovp = f0.data & Mask::Vac2Ovp;

        status.vsysOvp = f1.data & Mask::VsysOvp;
        status.vsysShort = f1.data & Mask::VsysShort;
        status.otgUvp = f1.data & Mask::OtgUvp;
        status.otgOvp = f1.data & Mask::OtgOvp;
        status.thermalShutdown = f1.data & Mask::Tshut;
        return status;
    }



    // ========== Info ==========

    Status::info<uint8> GetPartNumber() {
        auto read = ReadRegister(Address::PartInformation);
        StatusAssert(read.type);
        return (read.data & Mask::PartNum) >> Shift::PartNum;
    }


    Status::info<uint8> GetDeviceRevision() {
        auto read = ReadRegister(Address::PartInformation);
        StatusAssert(read.type);
        return (read.data & Mask::DevRev) >> Shift::DevRev;
    }


    Status::info<bool> IsPresent() {
        auto part = GetPartNumber();
        StatusAssert(part.type);
        return part.data == DeviceInfo::PartNumExpected;
    }



public:
    // ========== I2C ==========

    Status::statusType WriteRegister(uint8 reg, uint8 val) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
        }
        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, reg, 1, &val, 1);
    }


    Status::info<uint8> ReadRegister(uint8 reg) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
        }
        Status::info<uint8> read;
        read.type = i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, &read.data, 1);
        return read;
    }


    Status::statusType WriteRegister16(uint8 reg, uint16 val) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
        }
        uint8 buf[2] = {
            static_cast<uint8>(val >> 8),
            static_cast<uint8>(val & 0xFF)
        };
        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, reg, 1, buf, 2);
    }


    Status::info<uint16> ReadRegister16(uint8 reg) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
        }
        uint8 buf[2] = {};
        auto status = i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, buf, 2);
        return { status, static_cast<uint16>((buf[0] << 8) | buf[1]) };
    }


    Status::statusType UpdateRegisterBits(uint8 reg, uint8 mask, uint8 data) {
        auto read = ReadRegister(reg);
        if (read.IsError()) {
            return read.type;
        }
        read.data = (read.data & ~mask) | (data & mask);
        return WriteRegister(reg, read.data);
    }


    Status::info<bool> CheckBitRegister(uint8 reg, uint8 mask) {
        auto read = ReadRegister(reg);
        StatusAssert(read.type);
        return read.data & mask;
    }
};
