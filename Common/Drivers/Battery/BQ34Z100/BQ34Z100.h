#pragma once
#include <VHAL.h>
#include "BQ34Z100Registers.h"
#include <math.h>


using Address = BQ34Z100Registers::Address;
using ControlCmd = BQ34Z100Registers::ControlCmd;
using UnsealKey = BQ34Z100Registers::UnsealKey;
using FlagsBit = BQ34Z100Registers::FlagsBit;
using ControlStatusBit = BQ34Z100Registers::ControlStatusBit;
using FlashSubclass = BQ34Z100Registers::FlashSubclass;
using DeviceInfo = BQ34Z100Registers::DeviceInfo;

class BQ34Z100 {
public:
    struct VoltageMeasurement {
        uint16 mv = 0;
        float v() const { return mv / 1000.0f; }
    };

    struct CurrentMeasurement {
        int16 ma = 0;
        float a() const { return ma / 1000.0f; }
    };

    struct TemperatureMeasurement {
        uint16 deciK = 0; // 0.1 K
        float c() const { return (deciK - 2731) / 10.0f; }
        float k() const { return deciK / 10.0f; }
    };

    struct CapacityMeasurement {
        uint16 mah = 0;
        float ah() const { return mah / 1000.0f; }
    };

    struct TimeMeasurement {
        uint16 minutes = 0;
        float hours() const { return minutes / 60.0f; }
    };

    struct GaugeData {
        uint8 soc = 0;                       // State of charge, %
        uint8 soh = 0;                       // State of health, %
        VoltageMeasurement voltage;
        CurrentMeasurement current;
        CurrentMeasurement averageCurrent;
        TemperatureMeasurement temperature;
        CapacityMeasurement remaining;
        CapacityMeasurement fullCharge;
        TimeMeasurement timeToEmpty;
        TimeMeasurement timeToFull;
    };

    struct FlagsStatus {
        bool overTempCharge;
        bool overTempDischarge;
        bool batteryHigh;
        bool batteryLow;
        bool chargeInhibit;
        bool fullCharge;
        bool allowCharging;
        bool ocvTaken;
        bool conditionFlag;
        bool soc1;
        bool socFinal;
        bool discharging;
    };


private:
    AI2C *i2c = nullptr;
    uint8 flashBlock[32] = {};


public:
    BQ34Z100() { }
    BQ34Z100(AI2C& _i2c): i2c(&_i2c) { }



    // ========== Gauge readings ==========

    Result<uint8> GetStateOfCharge() {
        auto read = ReadRegister8(Address::StateOfCharge);
        if (read.IsErr()) return read.Error();
        return read.Value();
    }


    Result<uint8> GetMaxError() {
        auto read = ReadRegister8(Address::MaxError);
        if (read.IsErr()) return read.Error();
        return read.Value();
    }


    Result<VoltageMeasurement> GetVoltage() {
        auto read = ReadRegister16(Address::Voltage);
        if (read.IsErr()) return read.Error();
        return VoltageMeasurement{ read.Value() };
    }


    Result<CurrentMeasurement> GetCurrent() {
        auto read = ReadRegister16(Address::Current);
        if (read.IsErr()) return read.Error();
        return CurrentMeasurement{ static_cast<int16>(read.Value()) };
    }


    Result<CurrentMeasurement> GetAverageCurrent() {
        auto read = ReadRegister16(Address::AverageCurrent);
        if (read.IsErr()) return read.Error();
        return CurrentMeasurement{ static_cast<int16>(read.Value()) };
    }


    Result<TemperatureMeasurement> GetTemperature() {
        auto read = ReadRegister16(Address::Temperature);
        if (read.IsErr()) return read.Error();
        return TemperatureMeasurement{ read.Value() };
    }


    Result<TemperatureMeasurement> GetInternalTemperature() {
        auto read = ReadRegister16(Address::InternalTemperature);
        if (read.IsErr()) return read.Error();
        return TemperatureMeasurement{ read.Value() };
    }


    Result<CapacityMeasurement> GetRemainingCapacity() {
        auto read = ReadRegister16(Address::RemainingCapacity);
        if (read.IsErr()) return read.Error();
        return CapacityMeasurement{ read.Value() };
    }


    Result<CapacityMeasurement> GetFullChargeCapacity() {
        auto read = ReadRegister16(Address::FullChargeCapacity);
        if (read.IsErr()) return read.Error();
        return CapacityMeasurement{ read.Value() };
    }


    Result<CapacityMeasurement> GetDesignCapacity() {
        auto read = ReadRegister16(Address::DesignCapacity);
        if (read.IsErr()) return read.Error();
        return CapacityMeasurement{ read.Value() };
    }


    Result<TimeMeasurement> GetTimeToEmpty() {
        auto read = ReadRegister16(Address::AverageTimeToEmpty);
        if (read.IsErr()) return read.Error();
        return TimeMeasurement{ read.Value() };
    }


    Result<TimeMeasurement> GetTimeToFull() {
        auto read = ReadRegister16(Address::AverageTimeToFull);
        if (read.IsErr()) return read.Error();
        return TimeMeasurement{ read.Value() };
    }


    Result<uint16> GetStateOfHealth() {
        auto read = ReadRegister16(Address::StateOfHealth);
        if (read.IsErr()) return read.Error();
        return static_cast<uint16>(read.Value() & 0xFF); // low byte = %
    }


    Result<uint16> GetCycleCount() {
        return ReadRegister16(Address::CycleCount);
    }


    Result<VoltageMeasurement> GetChargeVoltage() {
        auto read = ReadRegister16(Address::ChargeVoltage);
        if (read.IsErr()) return read.Error();
        return VoltageMeasurement{ read.Value() };
    }


    Result<CurrentMeasurement> GetChargeCurrent() {
        auto read = ReadRegister16(Address::ChargeCurrent);
        if (read.IsErr()) return read.Error();
        return CurrentMeasurement{ static_cast<int16>(read.Value()) };
    }


    Result<GaugeData> ReadAll() {
        GaugeData data{};

        auto soc = ReadRegister8(Address::StateOfCharge);
        if (soc.IsErr()) return soc.Error();
        data.soc = soc.Value();

        auto volt = ReadRegister16(Address::Voltage);
        if (volt.IsErr()) return volt.Error();
        data.voltage.mv = volt.Value();

        auto cur = ReadRegister16(Address::Current);
        if (cur.IsErr()) return cur.Error();
        data.current.ma = static_cast<int16>(cur.Value());

        auto avgCur = ReadRegister16(Address::AverageCurrent);
        if (avgCur.IsErr()) return avgCur.Error();
        data.averageCurrent.ma = static_cast<int16>(avgCur.Value());

        auto temp = ReadRegister16(Address::Temperature);
        if (temp.IsErr()) return temp.Error();
        data.temperature.deciK = temp.Value();

        auto rem = ReadRegister16(Address::RemainingCapacity);
        if (rem.IsErr()) return rem.Error();
        data.remaining.mah = rem.Value();

        auto fcc = ReadRegister16(Address::FullChargeCapacity);
        if (fcc.IsErr()) return fcc.Error();
        data.fullCharge.mah = fcc.Value();

        auto tte = ReadRegister16(Address::AverageTimeToEmpty);
        if (tte.IsErr()) return tte.Error();
        data.timeToEmpty.minutes = tte.Value();

        auto ttf = ReadRegister16(Address::AverageTimeToFull);
        if (ttf.IsErr()) return ttf.Error();
        data.timeToFull.minutes = ttf.Value();

        auto soh = ReadRegister16(Address::StateOfHealth);
        if (soh.IsErr()) return soh.Error();
        data.soh = static_cast<uint8>(soh.Value() & 0xFF);

        return data;
    }



    // ========== Flags ==========

    Result<uint16> GetFlags() {
        return ReadRegister16(Address::Flags);
    }


    Result<uint16> GetFlagsB() {
        return ReadRegister16(Address::FlagsB);
    }


    Result<FlagsStatus> GetFlagsDecoded() {
        auto read = ReadRegister16(Address::Flags);
        if (read.IsErr()) return read.Error();

        FlagsStatus flags{};
        flags.overTempCharge   = read.Value() & FlagsBit::OTC;
        flags.overTempDischarge = read.Value() & FlagsBit::OTD;
        flags.batteryHigh      = read.Value() & FlagsBit::BATHI;
        flags.batteryLow       = read.Value() & FlagsBit::BATLOW;
        flags.chargeInhibit    = read.Value() & FlagsBit::CHG_INH;
        flags.fullCharge       = read.Value() & FlagsBit::FC;
        flags.allowCharging    = read.Value() & FlagsBit::CHG;
        flags.ocvTaken         = read.Value() & FlagsBit::OCVTAKEN;
        flags.conditionFlag    = read.Value() & FlagsBit::CF;
        flags.soc1             = read.Value() & FlagsBit::SOC1;
        flags.socFinal         = read.Value() & FlagsBit::SOCF;
        flags.discharging      = read.Value() & FlagsBit::DSG;
        return flags;
    }



    // ========== Control commands ==========

    Result<uint16> GetControlStatus() {
        return SendControlCommand(ControlCmd::ControlStatus);
    }


    Result<uint16> GetDeviceType() {
        return SendControlCommand(ControlCmd::DeviceType);
    }


    Result<uint16> GetFWVersion() {
        return SendControlCommand(ControlCmd::FWVersion);
    }


    Result<uint16> GetHWVersion() {
        return SendControlCommand(ControlCmd::HWVersion);
    }


    Result<uint16> GetChemID() {
        return SendControlCommand(ControlCmd::ChemID);
    }


    Result<uint16> GetDFVersion() {
        return SendControlCommand(ControlCmd::DFVersion);
    }


    ResultStatus EnableIT() {
        auto result = SendControlCommand(ControlCmd::ITEnable);
        if (result.IsErr()) return result.Error();
        return ResultStatus::ok;
    }


    ResultStatus ResetDevice() {
        auto result = SendControlCommand(ControlCmd::Reset);
        if (result.IsErr()) return result.Error();
        return ResultStatus::ok;
    }


    ResultStatus Seal() {
        auto result = SendControlCommand(ControlCmd::Sealed);
        if (result.IsErr()) return result.Error();
        return ResultStatus::ok;
    }


    ResultStatus Unseal() {
        auto s1 = SendControlCommand(UnsealKey::First);
        if (s1.IsErr()) return s1.Error();
        auto s2 = SendControlCommand(UnsealKey::Second);
        if (s2.IsErr()) return s2.Error();
        return ResultStatus::ok;
    }


    Result<bool> IsSealed() {
        auto status = GetControlStatus();
        if (status.IsErr()) return status.Error();
        return (status.Value() & ControlStatusBit::SS) != 0;
    }


    Result<bool> IsPresent() {
        auto type = GetDeviceType();
        if (type.IsErr()) return type.Error();
        return type.Value() == DeviceInfo::ExpectedDeviceType;
    }



    // ========== Calibration ==========

    ResultStatus EnterCalibration() {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto calEn = SendControlCommand(ControlCmd::CalEnable);
        if (calEn.IsErr()) return calEn.Error();

        auto enterCal = SendControlCommand(ControlCmd::EnterCal);
        if (enterCal.IsErr()) return enterCal.Error();
        return ResultStatus::ok;
    }


    ResultStatus ExitCalibration() {
        auto exitCal = SendControlCommand(ControlCmd::ExitCal);
        if (exitCal.IsErr()) return exitCal.Error();
        return ResultStatus::ok;
    }


    ResultStatus CalibrateCC() {
        auto result = SendControlCommand(ControlCmd::CCOffset);
        if (result.IsErr()) return result.Error();
        return ResultStatus::ok;
    }


    ResultStatus CalibrateCCSave() {
        auto result = SendControlCommand(ControlCmd::CCOffsetSave);
        if (result.IsErr()) return result.Error();
        return ResultStatus::ok;
    }


    ResultStatus CalibrateBoard() {
        auto result = SendControlCommand(ControlCmd::BoardOffset);
        if (result.IsErr()) return result.Error();
        return ResultStatus::ok;
    }



    // ========== Flash configuration ==========

    ResultStatus ReadFlashBlock(uint8 subclass, uint8 offset) {
        auto s1 = WriteRegister(Address::BlockDataControl, 0x00);
        StatusAssert(s1);

        auto s2 = WriteRegister(Address::DataFlashClass, subclass);
        StatusAssert(s2);

        auto s3 = WriteRegister(Address::DataFlashBlock, offset / 32);
        StatusAssert(s3);

        return i2c->ReadByteArray(DeviceInfo::SlaveAddress, Address::BlockData, 1, flashBlock, 32);
    }


    ResultStatus WriteFlashBlock(uint8 subclass, uint8 offset) {
        auto s1 = WriteRegister(Address::BlockDataControl, 0x00);
        StatusAssert(s1);

        auto s2 = WriteRegister(Address::DataFlashClass, subclass);
        StatusAssert(s2);

        auto s3 = WriteRegister(Address::DataFlashBlock, offset / 32);
        StatusAssert(s3);

        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, Address::BlockData, 1, flashBlock, 32);
    }


    ResultStatus CommitFlashBlock() {
        uint8 checksum = 0;
        for (int i = 0; i < 32; i++) {
            checksum += flashBlock[i];
        }
        checksum = 255 - checksum;
        return WriteRegister(Address::BlockDataChecksum, checksum);
    }


    uint8* GetFlashBlockData() {
        return flashBlock;
    }


    void SetFlashByte(uint8 offset, uint8 value) {
        flashBlock[offset % 32] = value;
    }


    void SetFlashWord(uint8 offset, uint16 value) {
        offset = offset % 32;
        flashBlock[offset]     = value >> 8;
        flashBlock[offset + 1] = value & 0xFF;
    }


    void SetFlashDword(uint8 offset, uint32 value) {
        offset = offset % 32;
        flashBlock[offset]     = value >> 24;
        flashBlock[offset + 1] = value >> 16;
        flashBlock[offset + 2] = value >> 8;
        flashBlock[offset + 3] = value & 0xFF;
    }


    uint16 GetFlashWord(uint8 offset) {
        offset = offset % 32;
        return (static_cast<uint16>(flashBlock[offset]) << 8) | flashBlock[offset + 1];
    }


    uint32 GetFlashDword(uint8 offset) {
        offset = offset % 32;
        return (static_cast<uint32>(flashBlock[offset]) << 24) |
               (static_cast<uint32>(flashBlock[offset + 1]) << 16) |
               (static_cast<uint32>(flashBlock[offset + 2]) << 8) |
               static_cast<uint32>(flashBlock[offset + 3]);
    }



    // ========== Flash high-level configuration ==========

    ResultStatus UpdateDesignCapacity(uint16 capacityMAh) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::DataClass, 0);
        StatusAssert(read);

        SetFlashWord(11, capacityMAh);
        SetFlashWord(8, capacityMAh); // CC threshold

        auto write = WriteFlashBlock(FlashSubclass::DataClass, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    ResultStatus UpdateDesignEnergy(uint16 energyMWh) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::DataClass, 0);
        StatusAssert(read);

        SetFlashWord(13, energyMWh);

        auto write = WriteFlashBlock(FlashSubclass::DataClass, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    ResultStatus UpdateSeriesCells(uint8 cells) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(read);

        SetFlashByte(7, cells);

        auto write = WriteFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    ResultStatus UpdatePackConfiguration(uint16 config) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(read);

        SetFlashWord(0, config);

        auto write = WriteFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    ResultStatus UpdateQMax(uint16 qmax) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::QMax, 0);
        StatusAssert(read);

        SetFlashWord(0, qmax);

        auto write = WriteFlashBlock(FlashSubclass::QMax, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    Result<uint16> ReadVoltageDivider() {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(read);

        return GetFlashWord(14);
    }


    ResultStatus UpdateVoltageDivider(uint16 divider) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(read);

        SetFlashWord(14, divider);

        auto write = WriteFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    ResultStatus UpdateCurrentGain(float senseResistorOhms) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(read);

        float gainDF = 4.768f / senseResistorOhms;
        float deltaDF = 5677445.6f / senseResistorOhms;

        SetFlashDword(0, FloatToXemics(gainDF));
        SetFlashDword(4, FloatToXemics(deltaDF));

        auto write = WriteFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    Result<float> ReadSenseResistor() {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(read);

        uint32 ccGain = GetFlashDword(0);
        return 4.768f / XemicsToFloat(ccGain);
    }



    // ========== Xemics float conversion ==========

    static uint32 FloatToXemics(float value) {
        bool negative = false;
        if (value == 0) value = 0.00001f;
        if (value < 0) {
            negative = true;
            value = -value;
        }

        int exp = static_cast<int>(logf(value) / logf(2.0f)) + 1;
        float mantissa = value / powf(2.0f, static_cast<float>(exp));
        mantissa = mantissa / powf(2.0f, -24.0f);

        uint8 byte1 = static_cast<uint8>(exp + 128);
        uint8 byte2 = static_cast<uint8>(static_cast<uint32>(mantissa) >> 16);
        uint8 byte3 = static_cast<uint8>(static_cast<uint32>(mantissa) >> 8);
        uint8 byte4 = static_cast<uint8>(static_cast<uint32>(mantissa));

        if (!negative) {
            byte2 &= 0x7F;
        }

        return (static_cast<uint32>(byte1) << 24) |
               (static_cast<uint32>(byte2) << 16) |
               (static_cast<uint32>(byte3) << 8) |
               static_cast<uint32>(byte4);
    }


    static float XemicsToFloat(uint32 value) {
        bool positive = !(value & 0x800000);
        int16 expGain = static_cast<int16>((value >> 24) - 128 - 24);
        float exponent = powf(2.0f, static_cast<float>(expGain));
        float mantissa = static_cast<float>((value & 0xFFFFFF) | 0x800000);

        float result = mantissa * exponent;
        return positive ? result : -result;
    }



public:
    // ========== I2C ==========

    ResultStatus WriteRegister(uint8 reg, uint8 val) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, reg, 1, &val, 1);
    }


    Result<uint8> ReadRegister8(uint8 reg) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        uint8 data;
        return Result<uint8>::Capture(i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, &data, 1), data);
    }


    // BQ34Z100 uses LSB-first 16-bit registers
    Result<uint16> ReadRegister16(uint8 reg) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        uint8 buf[2] = {};
        auto status = i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, buf, 2);
        return { status, static_cast<uint16>(buf[0] | (buf[1] << 8)) };
    }


    Result<uint16> SendControlCommand(uint16 cmd) {
        if (i2c == nullptr) {
            return ResultStatus::invalidParameter;
        }
        uint8 buf[2] = {
            static_cast<uint8>(cmd & 0xFF),
            static_cast<uint8>(cmd >> 8)
        };
        auto writeStatus = i2c->WriteByteArray(DeviceInfo::SlaveAddress, Address::Control, 1, buf, 2);
        StatusAssert(writeStatus);

        return ReadRegister16(Address::Control);
    }
};
