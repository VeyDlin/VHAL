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

    ResultStatus EnableCharging(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnChg, enable ? Mask::EnChg : 0);
    }


    Result<bool> IsChargingEnabled() {
        return CheckBitRegister(Address::ChargerCtrl0, Mask::EnChg);
    }


    ResultStatus SetChargeVoltage(float voltage) {
        uint16 regValue = ConfigConstants::ChargeVoltage.Encode(voltage);
        return WriteRegister16(Address::ChargeVoltageLimit, regValue);
    }


    Result<float> GetChargeVoltage() {
        auto read = ReadRegister16(Address::ChargeVoltageLimit);
        if (read.IsErr()) return read.Error();
        return ConfigConstants::ChargeVoltage.Decode(read.Value() & 0x07FF);
    }


    ResultStatus SetChargeCurrent(float current) {
        uint16 regValue = ConfigConstants::ChargeCurrent.Encode(current);
        return WriteRegister16(Address::ChargeCurrentLimit, regValue);
    }


    Result<float> GetChargeCurrent() {
        auto read = ReadRegister16(Address::ChargeCurrentLimit);
        if (read.IsErr()) return read.Error();
        return ConfigConstants::ChargeCurrent.Decode(read.Value() & 0x01FF);
    }


    ResultStatus SetTerminationCurrent(float current) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::TermCurrent.Encode(current));
        return UpdateRegisterBits(Address::TerminationControl, Mask::Iterm, regValue << Shift::Iterm);
    }


    ResultStatus EnableTermination(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnTerm, enable ? Mask::EnTerm : 0);
    }


    ResultStatus SetPrechargeCurrent(float current) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::PrechargeCurrent.Encode(current));
        return UpdateRegisterBits(Address::PrechargeControl, Mask::Iprechg, regValue << Shift::Iprechg);
    }


    ResultStatus SetPrechargeThreshold(VbatLowV threshold) {
        return UpdateRegisterBits(Address::PrechargeControl, Mask::VbatLowV, static_cast<uint8>(threshold) << Shift::VbatLowV);
    }


    ResultStatus SetRechargeOffset(float voltage) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::RechargeOffset.Encode(voltage));
        return UpdateRegisterBits(Address::RechargeControl, Mask::Vrechg, regValue << Shift::Vrechg);
    }


    ResultStatus SetCellCount(CellCount cells) {
        return UpdateRegisterBits(Address::RechargeControl, Mask::CellCount, static_cast<uint8>(cells) << Shift::CellCount);
    }



    // ========== Input limits ==========

    ResultStatus SetInputVoltageLimit(float voltage) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::InputVoltage.Encode(voltage));
        return WriteRegister(Address::InputVoltageLimit, regValue);
    }


    ResultStatus SetInputCurrentLimit(float current) {
        uint16 regValue = ConfigConstants::InputCurrent.Encode(current);
        return WriteRegister16(Address::InputCurrentLimit, regValue);
    }


    Result<float> GetInputCurrentLimit() {
        auto read = ReadRegister16(Address::InputCurrentLimit);
        if (read.IsErr()) return read.Error();
        return ConfigConstants::InputCurrent.Decode(read.Value() & 0x01FF);
    }


    ResultStatus SetMinSystemVoltage(float voltage) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::MinSystemV.Encode(voltage));
        return UpdateRegisterBits(Address::MinSystemVoltage, Mask::MinSysV, regValue);
    }


    ResultStatus SetVacOvp(VacOvp threshold) {
        return UpdateRegisterBits(Address::ChargerCtrl1, Mask::VacOvp, static_cast<uint8>(threshold) << Shift::VacOvp);
    }



    // ========== Power path ==========

    ResultStatus SetHighImpedanceMode(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnHiz, enable ? Mask::EnHiz : 0);
    }


    Result<bool> IsInHighImpedanceMode() {
        return CheckBitRegister(Address::ChargerCtrl0, Mask::EnHiz);
    }



    // ========== OTG mode ==========

    ResultStatus EnableOtg(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl3, Mask::EnOtg, enable ? Mask::EnOtg : 0);
    }


    ResultStatus SetOtgVoltage(float voltage) {
        uint16 regValue = ConfigConstants::OtgVoltage.Encode(voltage);
        return WriteRegister16(Address::OtgVoltage, regValue);
    }


    ResultStatus SetOtgCurrentLimit(float current) {
        uint8 regValue = static_cast<uint8>(ConfigConstants::OtgCurrent.Encode(current));
        return UpdateRegisterBits(Address::OtgRegulation, Mask::Iotg, regValue << Shift::Iotg);
    }



    // ========== Timers & watchdog ==========

    ResultStatus SetWatchdog(WatchdogTimer timer) {
        return UpdateRegisterBits(Address::ChargerCtrl1, Mask::Watchdog, static_cast<uint8>(timer) << Shift::Watchdog);
    }


    ResultStatus KickWatchdog() {
        return UpdateRegisterBits(Address::ChargerCtrl1, Mask::WdRst, Mask::WdRst);
    }


    ResultStatus EnableSafetyTimer(bool enable) {
        return UpdateRegisterBits(Address::TimerControl, Mask::EnChgTmr, enable ? Mask::EnChgTmr : 0);
    }


    ResultStatus SetSafetyTimer(FastChargeTimer timer) {
        return UpdateRegisterBits(Address::TimerControl, Mask::ChgTmr, static_cast<uint8>(timer) << Shift::ChgTmr);
    }



    // ========== Special modes ==========

    ResultStatus SetShipMode(ShipMode mode) {
        return UpdateRegisterBits(Address::ChargerCtrl2, Mask::SdrvCtrl, static_cast<uint8>(mode) << Shift::SdrvCtrl);
    }


    ResultStatus EnableBackupMode(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnBackup, enable ? Mask::EnBackup : 0);
    }


    ResultStatus EnableIco(bool enable) {
        return UpdateRegisterBits(Address::ChargerCtrl0, Mask::EnIco, enable ? Mask::EnIco : 0);
    }


    ResultStatus Reset() {
        return UpdateRegisterBits(Address::TerminationControl, Mask::RegRst, Mask::RegRst);
    }



    // ========== ADC ==========

    ResultStatus EnableAdc(bool enable, bool continuous = true) {
        uint8 val = 0;
        if (enable) {
            val |= Mask::AdcEn;
            if (!continuous) {
                val |= Mask::AdcRate;
            }
        }
        return UpdateRegisterBits(Address::AdcControl, Mask::AdcEn | Mask::AdcRate, val);
    }


    Result<AdcResult> ReadAdc() {
        AdcResult result{};

        auto ibus = ReadRegister16(Address::IbusAdc);
        if (ibus.IsErr()) return ibus.Error();
        result.ibus.ma = static_cast<int16>(ibus.Value());

        auto ibat = ReadRegister16(Address::IbatAdc);
        if (ibat.IsErr()) return ibat.Error();
        result.ibat.ma = static_cast<int16>(ibat.Value());

        auto vbus = ReadRegister16(Address::VbusAdc);
        if (vbus.IsErr()) return vbus.Error();
        result.vbus.mv = vbus.Value();

        auto vac1 = ReadRegister16(Address::Vac1Adc);
        if (vac1.IsErr()) return vac1.Error();
        result.vac1.mv = vac1.Value();

        auto vac2 = ReadRegister16(Address::Vac2Adc);
        if (vac2.IsErr()) return vac2.Error();
        result.vac2.mv = vac2.Value();

        auto vbat = ReadRegister16(Address::VbatAdc);
        if (vbat.IsErr()) return vbat.Error();
        result.vbat.mv = vbat.Value();

        auto vsys = ReadRegister16(Address::VsysAdc);
        if (vsys.IsErr()) return vsys.Error();
        result.vsys.mv = vsys.Value();

        auto tdie = ReadRegister16(Address::TdieAdc);
        if (tdie.IsErr()) return tdie.Error();
        result.tdie.raw = static_cast<int16>(tdie.Value());

        return result;
    }


    Result<VoltageMeasurement> ReadVbus() {
        auto read = ReadRegister16(Address::VbusAdc);
        if (read.IsErr()) return read.Error();
        return VoltageMeasurement{ read.Value() };
    }


    Result<VoltageMeasurement> ReadVbat() {
        auto read = ReadRegister16(Address::VbatAdc);
        if (read.IsErr()) return read.Error();
        return VoltageMeasurement{ read.Value() };
    }


    Result<CurrentMeasurement> ReadIbat() {
        auto read = ReadRegister16(Address::IbatAdc);
        if (read.IsErr()) return read.Error();
        return CurrentMeasurement{ static_cast<int16>(read.Value()) };
    }



    // ========== Status and faults ==========

    Result<ChargingStatus> GetChargingStatus() {
        auto read = ReadRegister(Address::ChargerStatus1);
        if (read.IsErr()) return read.Error();
        return static_cast<ChargingStatus>((read.Value() & Mask::ChgStat) >> Shift::ChgStat);
    }


    Result<PowerStatus> GetPowerStatus() {
        auto read = ReadRegister(Address::ChargerStatus0);
        if (read.IsErr()) return read.Error();

        PowerStatus status{};
        status.vbusPresent = read.Value() & Mask::VbusPresent;
        status.powerGood = read.Value() & Mask::PgStat;
        status.vindpmActive = read.Value() & Mask::VindpmStat;
        status.iindpmActive = read.Value() & Mask::IindpmStat;
        status.watchdogFault = read.Value() & Mask::WdStat;
        return status;
    }


    Result<FaultStatus> GetFaultStatus() {
        auto f0 = ReadRegister(Address::FaultStatus0);
        if (f0.IsErr()) return f0.Error();

        auto f1 = ReadRegister(Address::FaultStatus1);
        if (f1.IsErr()) return f1.Error();

        FaultStatus status{};
        status.vbusOvp = f0.Value() & Mask::VbusOvp;
        status.vbatOvp = f0.Value() & Mask::VbatOvp;
        status.ibusOcp = f0.Value() & Mask::IbusOcp;
        status.ibatOcp = f0.Value() & Mask::IbatOcp;
        status.convOcp = f0.Value() & Mask::ConvOcp;
        status.vac1Ovp = f0.Value() & Mask::Vac1Ovp;
        status.vac2Ovp = f0.Value() & Mask::Vac2Ovp;

        status.vsysOvp = f1.Value() & Mask::VsysOvp;
        status.vsysShort = f1.Value() & Mask::VsysShort;
        status.otgUvp = f1.Value() & Mask::OtgUvp;
        status.otgOvp = f1.Value() & Mask::OtgOvp;
        status.thermalShutdown = f1.Value() & Mask::Tshut;
        return status;
    }



    // ========== Info ==========

    Result<uint8> GetPartNumber() {
        auto read = ReadRegister(Address::PartInformation);
        if (read.IsErr()) return read.Error();
        return (read.Value() & Mask::PartNum) >> Shift::PartNum;
    }


    Result<uint8> GetDeviceRevision() {
        auto read = ReadRegister(Address::PartInformation);
        if (read.IsErr()) return read.Error();
        return (read.Value() & Mask::DevRev) >> Shift::DevRev;
    }


    Result<bool> IsPresent() {
        auto part = GetPartNumber();
        if (part.IsErr()) return part.Error();
        return part.Value() == DeviceInfo::PartNumExpected;
    }



public:
    // ========== I2C ==========

    ResultStatus WriteRegister(uint8 reg, uint8 val) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, reg, 1, &val, 1);
    }


    Result<uint8> ReadRegister(uint8 reg) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        uint8 data;
        return Result<uint8>::Capture(i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, &data, 1), data);
    }


    ResultStatus WriteRegister16(uint8 reg, uint16 val) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        uint8 buf[2] = {
            static_cast<uint8>(val >> 8),
            static_cast<uint8>(val & 0xFF)
        };
        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, reg, 1, buf, 2);
    }


    Result<uint16> ReadRegister16(uint8 reg) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        uint8 buf[2] = {};
        auto status = i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, buf, 2);
        return { status, static_cast<uint16>((buf[0] << 8) | buf[1]) };
    }


    ResultStatus UpdateRegisterBits(uint8 reg, uint8 mask, uint8 data) {
        auto read = ReadRegister(reg);
        if (read.IsErr()) {
            return read.Error();
        }
        uint8 val = (read.Value() & ~mask) | (data & mask);
        return WriteRegister(reg, val);
    }


    Result<bool> CheckBitRegister(uint8 reg, uint8 mask) {
        auto read = ReadRegister(reg);
        if (read.IsErr()) return read.Error();
        return (bool)(read.Value() & mask);
    }
};
