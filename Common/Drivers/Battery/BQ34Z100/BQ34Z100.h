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

    Status::info<uint8> GetStateOfCharge() {
        auto read = ReadRegister8(Address::StateOfCharge);
        StatusAssert(read.type);
        return read.data;
    }


    Status::info<uint8> GetMaxError() {
        auto read = ReadRegister8(Address::MaxError);
        StatusAssert(read.type);
        return read.data;
    }


    Status::info<VoltageMeasurement> GetVoltage() {
        auto read = ReadRegister16(Address::Voltage);
        StatusAssert(read.type);
        return VoltageMeasurement{ read.data };
    }


    Status::info<CurrentMeasurement> GetCurrent() {
        auto read = ReadRegister16(Address::Current);
        StatusAssert(read.type);
        return CurrentMeasurement{ static_cast<int16>(read.data) };
    }


    Status::info<CurrentMeasurement> GetAverageCurrent() {
        auto read = ReadRegister16(Address::AverageCurrent);
        StatusAssert(read.type);
        return CurrentMeasurement{ static_cast<int16>(read.data) };
    }


    Status::info<TemperatureMeasurement> GetTemperature() {
        auto read = ReadRegister16(Address::Temperature);
        StatusAssert(read.type);
        return TemperatureMeasurement{ read.data };
    }


    Status::info<TemperatureMeasurement> GetInternalTemperature() {
        auto read = ReadRegister16(Address::InternalTemperature);
        StatusAssert(read.type);
        return TemperatureMeasurement{ read.data };
    }


    Status::info<CapacityMeasurement> GetRemainingCapacity() {
        auto read = ReadRegister16(Address::RemainingCapacity);
        StatusAssert(read.type);
        return CapacityMeasurement{ read.data };
    }


    Status::info<CapacityMeasurement> GetFullChargeCapacity() {
        auto read = ReadRegister16(Address::FullChargeCapacity);
        StatusAssert(read.type);
        return CapacityMeasurement{ read.data };
    }


    Status::info<CapacityMeasurement> GetDesignCapacity() {
        auto read = ReadRegister16(Address::DesignCapacity);
        StatusAssert(read.type);
        return CapacityMeasurement{ read.data };
    }


    Status::info<TimeMeasurement> GetTimeToEmpty() {
        auto read = ReadRegister16(Address::AverageTimeToEmpty);
        StatusAssert(read.type);
        return TimeMeasurement{ read.data };
    }


    Status::info<TimeMeasurement> GetTimeToFull() {
        auto read = ReadRegister16(Address::AverageTimeToFull);
        StatusAssert(read.type);
        return TimeMeasurement{ read.data };
    }


    Status::info<uint16> GetStateOfHealth() {
        auto read = ReadRegister16(Address::StateOfHealth);
        StatusAssert(read.type);
        return static_cast<uint16>(read.data & 0xFF); // low byte = %
    }


    Status::info<uint16> GetCycleCount() {
        return ReadRegister16(Address::CycleCount);
    }


    Status::info<VoltageMeasurement> GetChargeVoltage() {
        auto read = ReadRegister16(Address::ChargeVoltage);
        StatusAssert(read.type);
        return VoltageMeasurement{ read.data };
    }


    Status::info<CurrentMeasurement> GetChargeCurrent() {
        auto read = ReadRegister16(Address::ChargeCurrent);
        StatusAssert(read.type);
        return CurrentMeasurement{ static_cast<int16>(read.data) };
    }


    Status::info<GaugeData> ReadAll() {
        GaugeData data{};

        auto soc = ReadRegister8(Address::StateOfCharge);
        StatusAssert(soc.type);
        data.soc = soc.data;

        auto volt = ReadRegister16(Address::Voltage);
        StatusAssert(volt.type);
        data.voltage.mv = volt.data;

        auto cur = ReadRegister16(Address::Current);
        StatusAssert(cur.type);
        data.current.ma = static_cast<int16>(cur.data);

        auto avgCur = ReadRegister16(Address::AverageCurrent);
        StatusAssert(avgCur.type);
        data.averageCurrent.ma = static_cast<int16>(avgCur.data);

        auto temp = ReadRegister16(Address::Temperature);
        StatusAssert(temp.type);
        data.temperature.deciK = temp.data;

        auto rem = ReadRegister16(Address::RemainingCapacity);
        StatusAssert(rem.type);
        data.remaining.mah = rem.data;

        auto fcc = ReadRegister16(Address::FullChargeCapacity);
        StatusAssert(fcc.type);
        data.fullCharge.mah = fcc.data;

        auto tte = ReadRegister16(Address::AverageTimeToEmpty);
        StatusAssert(tte.type);
        data.timeToEmpty.minutes = tte.data;

        auto ttf = ReadRegister16(Address::AverageTimeToFull);
        StatusAssert(ttf.type);
        data.timeToFull.minutes = ttf.data;

        auto soh = ReadRegister16(Address::StateOfHealth);
        StatusAssert(soh.type);
        data.soh = static_cast<uint8>(soh.data & 0xFF);

        return data;
    }



    // ========== Flags ==========

    Status::info<uint16> GetFlags() {
        return ReadRegister16(Address::Flags);
    }


    Status::info<uint16> GetFlagsB() {
        return ReadRegister16(Address::FlagsB);
    }


    Status::info<FlagsStatus> GetFlagsDecoded() {
        auto read = ReadRegister16(Address::Flags);
        StatusAssert(read.type);

        FlagsStatus flags{};
        flags.overTempCharge   = read.data & FlagsBit::OTC;
        flags.overTempDischarge = read.data & FlagsBit::OTD;
        flags.batteryHigh      = read.data & FlagsBit::BATHI;
        flags.batteryLow       = read.data & FlagsBit::BATLOW;
        flags.chargeInhibit    = read.data & FlagsBit::CHG_INH;
        flags.fullCharge       = read.data & FlagsBit::FC;
        flags.allowCharging    = read.data & FlagsBit::CHG;
        flags.ocvTaken         = read.data & FlagsBit::OCVTAKEN;
        flags.conditionFlag    = read.data & FlagsBit::CF;
        flags.soc1             = read.data & FlagsBit::SOC1;
        flags.socFinal         = read.data & FlagsBit::SOCF;
        flags.discharging      = read.data & FlagsBit::DSG;
        return flags;
    }



    // ========== Control commands ==========

    Status::info<uint16> GetControlStatus() {
        return SendControlCommand(ControlCmd::ControlStatus);
    }


    Status::info<uint16> GetDeviceType() {
        return SendControlCommand(ControlCmd::DeviceType);
    }


    Status::info<uint16> GetFWVersion() {
        return SendControlCommand(ControlCmd::FWVersion);
    }


    Status::info<uint16> GetHWVersion() {
        return SendControlCommand(ControlCmd::HWVersion);
    }


    Status::info<uint16> GetChemID() {
        return SendControlCommand(ControlCmd::ChemID);
    }


    Status::info<uint16> GetDFVersion() {
        return SendControlCommand(ControlCmd::DFVersion);
    }


    Status::statusType EnableIT() {
        auto result = SendControlCommand(ControlCmd::ITEnable);
        return result.type;
    }


    Status::statusType ResetDevice() {
        auto result = SendControlCommand(ControlCmd::Reset);
        return result.type;
    }


    Status::statusType Seal() {
        auto result = SendControlCommand(ControlCmd::Sealed);
        return result.type;
    }


    Status::statusType Unseal() {
        auto s1 = SendControlCommand(UnsealKey::First);
        StatusAssert(s1.type);
        auto s2 = SendControlCommand(UnsealKey::Second);
        return s2.type;
    }


    Status::info<bool> IsSealed() {
        auto status = GetControlStatus();
        StatusAssert(status.type);
        return (status.data & ControlStatusBit::SS) != 0;
    }


    Status::info<bool> IsPresent() {
        auto type = GetDeviceType();
        StatusAssert(type.type);
        return type.data == DeviceInfo::ExpectedDeviceType;
    }



    // ========== Calibration ==========

    Status::statusType EnterCalibration() {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto calEn = SendControlCommand(ControlCmd::CalEnable);
        StatusAssert(calEn.type);

        auto enterCal = SendControlCommand(ControlCmd::EnterCal);
        return enterCal.type;
    }


    Status::statusType ExitCalibration() {
        auto exitCal = SendControlCommand(ControlCmd::ExitCal);
        return exitCal.type;
    }


    Status::statusType CalibrateCC() {
        auto result = SendControlCommand(ControlCmd::CCOffset);
        return result.type;
    }


    Status::statusType CalibrateCCSave() {
        auto result = SendControlCommand(ControlCmd::CCOffsetSave);
        return result.type;
    }


    Status::statusType CalibrateBoard() {
        auto result = SendControlCommand(ControlCmd::BoardOffset);
        return result.type;
    }



    // ========== Flash configuration ==========

    Status::statusType ReadFlashBlock(uint8 subclass, uint8 offset) {
        auto s1 = WriteRegister(Address::BlockDataControl, 0x00);
        StatusAssert(s1);

        auto s2 = WriteRegister(Address::DataFlashClass, subclass);
        StatusAssert(s2);

        auto s3 = WriteRegister(Address::DataFlashBlock, offset / 32);
        StatusAssert(s3);

        return i2c->ReadByteArray(DeviceInfo::SlaveAddress, Address::BlockData, 1, flashBlock, 32);
    }


    Status::statusType WriteFlashBlock(uint8 subclass, uint8 offset) {
        auto s1 = WriteRegister(Address::BlockDataControl, 0x00);
        StatusAssert(s1);

        auto s2 = WriteRegister(Address::DataFlashClass, subclass);
        StatusAssert(s2);

        auto s3 = WriteRegister(Address::DataFlashBlock, offset / 32);
        StatusAssert(s3);

        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, Address::BlockData, 1, flashBlock, 32);
    }


    Status::statusType CommitFlashBlock() {
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

    Status::statusType UpdateDesignCapacity(uint16 capacityMAh) {
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


    Status::statusType UpdateDesignEnergy(uint16 energyMWh) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::DataClass, 0);
        StatusAssert(read);

        SetFlashWord(13, energyMWh);

        auto write = WriteFlashBlock(FlashSubclass::DataClass, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    Status::statusType UpdateSeriesCells(uint8 cells) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(read);

        SetFlashByte(7, cells);

        auto write = WriteFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    Status::statusType UpdatePackConfiguration(uint16 config) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(read);

        SetFlashWord(0, config);

        auto write = WriteFlashBlock(FlashSubclass::Registers, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    Status::statusType UpdateQMax(uint16 qmax) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::QMax, 0);
        StatusAssert(read);

        SetFlashWord(0, qmax);

        auto write = WriteFlashBlock(FlashSubclass::QMax, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    Status::info<uint16> ReadVoltageDivider() {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(read);

        return GetFlashWord(14);
    }


    Status::statusType UpdateVoltageDivider(uint16 divider) {
        auto unseal = Unseal();
        StatusAssert(unseal);

        auto read = ReadFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(read);

        SetFlashWord(14, divider);

        auto write = WriteFlashBlock(FlashSubclass::CalibrationData, 0);
        StatusAssert(write);

        return CommitFlashBlock();
    }


    Status::statusType UpdateCurrentGain(float senseResistorOhms) {
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


    Status::info<float> ReadSenseResistor() {
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

    Status::statusType WriteRegister(uint8 reg, uint8 val) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
        }
        return i2c->WriteByteArray(DeviceInfo::SlaveAddress, reg, 1, &val, 1);
    }


    Status::info<uint8> ReadRegister8(uint8 reg) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
        }
        Status::info<uint8> read;
        read.type = i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, &read.data, 1);
        return read;
    }


    // BQ34Z100 uses LSB-first 16-bit registers
    Status::info<uint16> ReadRegister16(uint8 reg) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
        }
        uint8 buf[2] = {};
        auto status = i2c->ReadByteArray(DeviceInfo::SlaveAddress, reg, 1, buf, 2);
        return { status, static_cast<uint16>(buf[0] | (buf[1] << 8)) };
    }


    Status::info<uint16> SendControlCommand(uint16 cmd) {
        if (i2c == nullptr) {
            return Status::invalidParameter;
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
