#pragma once

#include <VHAL.h>
#include <cstdint>


class BQ27441 {
public:
    static constexpr uint8 SlaveAddress = 0x55;

    // Delay between Flags() polls while CONFIG UPDATE mode is entered or exited.
    static constexpr uint32 FlagPollIntervalMs = 10;

    // TRM SLUUAC9 allows up to 1 second for CONFIG UPDATE entry and exit; 120 polls leave margin.
    static constexpr uint32 FlagPollLimit = 120;

    static constexpr uint16 MaxDesignCapacityMah = 8856;
    static constexpr uint16 MinTerminateVoltageMv = 2800;
    static constexpr uint16 MaxTerminateVoltageMv = 3700;
    static constexpr uint32 MaxTaperRate = 2000;

    using DelayFunction = void (*)(uint32 milliseconds);

    struct Flags {
        bool overTemperature = false;
        bool underTemperature = false;
        bool fullCharge = false;
        bool chargingAllowed = false;
        bool initialPowerOnReset = false;
        bool configurationUpdate = false;
        bool batteryDetected = false;
        bool stateOfChargeLow = false;
        bool stateOfChargeCritical = false;
        bool discharging = false;
    };

    struct Measurements {
        uint16 voltageMv = 0;
        std::int16_t averageCurrentMa = 0;
        uint16 stateOfChargePercent = 0;
        uint16 temperatureDeciKelvin = 0;
        uint16 rawFlags = 0;
        Flags flags{};
    };

    // Battery pack parameters written into Data Memory by Configure().
    // Design Energy is derived as capacity x 3.7 V and Taper Rate as capacity x 10 / taper current.
    struct Profile {
        uint16 designCapacityMah = 0;
        uint16 terminateVoltageMv = 0;
        uint16 taperCurrentMa = 0;


        bool IsValid() const {
            if (designCapacityMah == 0 || designCapacityMah > MaxDesignCapacityMah) {
                return false;
            }
            if (terminateVoltageMv < MinTerminateVoltageMv || terminateVoltageMv > MaxTerminateVoltageMv) {
                return false;
            }
            if (taperCurrentMa == 0) {
                return false;
            }
            const uint32 taperRate = static_cast<uint32>(designCapacityMah) * 10U / taperCurrentMa;
            return taperRate >= 1 && taperRate <= MaxTaperRate;
        }
    };

private:
    static constexpr uint8 RegisterControl = 0x00;
    static constexpr uint8 RegisterFlags = 0x06;
    static constexpr uint8 RegisterDataControl = 0x61;
    static constexpr uint8 RegisterDataClass = 0x3E;
    static constexpr uint8 RegisterDataBlock = 0x3F;
    static constexpr uint8 RegisterBlockData = 0x40;
    static constexpr uint8 RegisterBlockChecksum = 0x60;

    static constexpr uint8 BlockSize = 32;

    static constexpr uint16 ControlUnseal = 0x8000;
    static constexpr uint16 ControlSetConfigUpdate = 0x0013;
    static constexpr uint16 ControlSoftReset = 0x0042;
    static constexpr uint16 ControlSeal = 0x0020;
    static constexpr uint16 ControlBatInsert = 0x000C;

    static constexpr uint16 FlagInitialPowerOnReset = 0x0020;
    static constexpr uint16 FlagConfigUpdate = 0x0010;
    static constexpr uint16 FlagBatteryDetected = 0x0008;

    // Data Memory classes and offsets (block offsets are relative to the 32-byte block).
    static constexpr uint8 ClassRegisters = 64;
    static constexpr uint8 ClassState = 82;
    static constexpr uint8 StateDesignCapacity = 10;
    static constexpr uint8 StateDesignEnergy = 12;
    static constexpr uint8 StateTerminateVoltage = 16;
    static constexpr uint8 StateTaperRate = 27;
    static constexpr uint8 RegistersOpConfig = 0;

    // OpConfig[BIE] is bit 5 of the high byte (SLUUAC9 table 6-6, default 0x25F8). With BIE cleared,
    // BAT_INSERT is the battery presence source. Low-byte bit 3 is RSVD1 and must stay set.
    static constexpr uint16 OpConfigBieMask = 0x2000;

    AI2C& i2c;
    DelayFunction delayFunction = nullptr;

public:
    explicit BQ27441(AI2C& bus, DelayFunction delay = nullptr): i2c(bus), delayFunction(delay) { }


    Result<Measurements> ReadMeasurements() {
        Result<uint16> temperature = ReadWord(0x02);
        if (temperature.IsErr()) {
            return temperature.Error();
        }
        Result<uint16> voltage = ReadWord(0x04);
        if (voltage.IsErr()) {
            return voltage.Error();
        }
        Result<uint16> flags = ReadWord(0x06);
        if (flags.IsErr()) {
            return flags.Error();
        }
        Result<uint16> current = ReadWord(0x10);
        if (current.IsErr()) {
            return current.Error();
        }
        Result<uint16> charge = ReadWord(0x1C);
        if (charge.IsErr()) {
            return charge.Error();
        }
        Measurements result{};
        result.temperatureDeciKelvin = temperature.Value();
        result.voltageMv = voltage.Value();
        result.averageCurrentMa = static_cast<std::int16_t>(current.Value());
        result.stateOfChargePercent = charge.Value();
        result.rawFlags = flags.Value();
        result.flags.overTemperature = (flags.Value() & 0x8000) != 0;
        result.flags.underTemperature = (flags.Value() & 0x4000) != 0;
        result.flags.fullCharge = (flags.Value() & 0x0200) != 0;
        result.flags.chargingAllowed = (flags.Value() & 0x0100) != 0;
        result.flags.initialPowerOnReset = (flags.Value() & 0x0020) != 0;
        result.flags.configurationUpdate = (flags.Value() & 0x0010) != 0;
        result.flags.batteryDetected = (flags.Value() & 0x0008) != 0;
        result.flags.stateOfChargeLow = (flags.Value() & 0x0004) != 0;
        result.flags.stateOfChargeCritical = (flags.Value() & 0x0002) != 0;
        result.flags.discharging = (flags.Value() & 0x0001) != 0;
        return result;
    }


    // True when Flags()[ITPOR] is set (RAM holds ROM defaults) or Flags()[BAT_DET] is clear.
    Result<bool> NeedsConfiguration() {
        Result<uint16> flags = ReadWord(RegisterFlags);
        if (flags.IsErr()) {
            return flags.Error();
        }
        const bool initialPowerOnReset = (flags.Value() & FlagInitialPowerOnReset) != 0;
        const bool batteryDetected = (flags.Value() & FlagBatteryDetected) != 0;
        return initialPowerOnReset || !batteryDetected;
    }


    // TI SLUUAC9 section 3.1 sequence: UNSEAL, SET_CFGUPDATE, write State (82) and Registers (64) blocks
    // with checksums, SOFT_RESET, SEAL, BAT_INSERT. Every wait is bounded by FlagPollLimit polls.
    // On failure after entering CONFIG UPDATE the routine still tries to leave it and to re-seal.
    ResultStatus Configure(const Profile &profile) {
        if (!profile.IsValid()) {
            return ResultStatus::invalidParameter;
        }
        bool configUpdateRequested = false;
        ResultStatus status = SendControl(ControlUnseal);
        if (status == ResultStatus::ok) {
            status = SendControl(ControlUnseal);
        }
        if (status == ResultStatus::ok) {
            configUpdateRequested = true;
            status = SendControl(ControlSetConfigUpdate);
        }
        if (status == ResultStatus::ok) {
            status = WaitForFlag(FlagConfigUpdate, true);
        }
        if (status == ResultStatus::ok) {
            status = WriteDataMemory(profile);
        }
        if (configUpdateRequested) {
            const ResultStatus exitStatus = ExitConfigUpdate();
            if (status == ResultStatus::ok) {
                status = exitStatus;
            }
        }
        const ResultStatus sealStatus = SendControl(ControlSeal);
        if (status == ResultStatus::ok) {
            status = sealStatus;
        }
        if (status == ResultStatus::ok) {
            status = SendControl(ControlBatInsert);
        }
        return status;
    }

private:
    Result<uint16> ReadWord(uint8 address) {
        uint8 data[2]{};
        ResultStatus result = i2c.ReadByteArray(SlaveAddress, address, 1, data, 2);
        if (result != ResultStatus::ok) {
            return result;
        }
        return static_cast<uint16>(data[0] | (static_cast<uint16>(data[1]) << 8));
    }


    // Control() subcommands are sent little-endian in two bytes to register 0x00.
    ResultStatus SendControl(uint16 subcommand) {
        uint8 data[2] = {static_cast<uint8>(subcommand & 0xFF), static_cast<uint8>(subcommand >> 8)};
        return i2c.WriteByteArray(SlaveAddress, RegisterControl, 1, data, 2);
    }


    ResultStatus WriteRegister(uint8 address, uint8 value) {
        uint8 data = value;
        return i2c.WriteByteArray(SlaveAddress, address, 1, &data, 1);
    }


    ResultStatus WaitForFlag(uint16 mask, bool expectedSet) {
        for (uint32 poll = 0; poll < FlagPollLimit; poll++) {
            Result<uint16> flags = ReadWord(RegisterFlags);
            if (flags.IsErr()) {
                return flags.Error();
            }
            if (((flags.Value() & mask) != 0) == expectedSet) {
                return ResultStatus::ok;
            }
            if (poll + 1 < FlagPollLimit && delayFunction != nullptr) {
                delayFunction(FlagPollIntervalMs);
            }
        }
        return ResultStatus::timeout;
    }


    ResultStatus ExitConfigUpdate() {
        ResultStatus status = SendControl(ControlSoftReset);
        if (status != ResultStatus::ok) {
            return status;
        }
        return WaitForFlag(FlagConfigUpdate, false);
    }


    ResultStatus WriteDataMemory(const Profile &profile) {
        ResultStatus status = WriteRegister(RegisterDataControl, 0x00);
        if (status == ResultStatus::ok) {
            status = WriteStateSubclass(profile);
        }
        if (status == ResultStatus::ok) {
            status = WriteRegistersSubclass();
        }
        return status;
    }


    ResultStatus WriteStateSubclass(const Profile &profile) {
        uint8 block[BlockSize]{};
        ResultStatus status = SelectBlock(ClassState, 0);
        if (status != ResultStatus::ok) {
            return status;
        }
        status = ReadBlock(block);
        if (status != ResultStatus::ok) {
            return status;
        }
        const uint32 designEnergy = static_cast<uint32>(profile.designCapacityMah) * 37U / 10U;
        const uint32 taperRate = static_cast<uint32>(profile.designCapacityMah) * 10U / profile.taperCurrentMa;
        PutBigEndian16(block, StateDesignCapacity, profile.designCapacityMah);
        PutBigEndian16(block, StateDesignEnergy, static_cast<uint16>(designEnergy));
        PutBigEndian16(block, StateTerminateVoltage, profile.terminateVoltageMv);
        PutBigEndian16(block, StateTaperRate, static_cast<uint16>(taperRate));
        return WriteBlock(block);
    }


    ResultStatus WriteRegistersSubclass() {
        uint8 block[BlockSize]{};
        ResultStatus status = SelectBlock(ClassRegisters, 0);
        if (status != ResultStatus::ok) {
            return status;
        }
        status = ReadBlock(block);
        if (status != ResultStatus::ok) {
            return status;
        }
        const uint16 opConfig = GetBigEndian16(block, RegistersOpConfig);
        PutBigEndian16(block, RegistersOpConfig, static_cast<uint16>(opConfig & ~OpConfigBieMask));
        return WriteBlock(block);
    }


    ResultStatus SelectBlock(uint8 dataClass, uint8 blockIndex) {
        ResultStatus status = WriteRegister(RegisterDataClass, dataClass);
        if (status != ResultStatus::ok) {
            return status;
        }
        return WriteRegister(RegisterDataBlock, blockIndex);
    }


    ResultStatus ReadBlock(uint8 (&block)[BlockSize]) {
        return i2c.ReadByteArray(SlaveAddress, RegisterBlockData, 1, block, BlockSize);
    }


    // The block is transferred to Data Memory only when its checksum is written to 0x60.
    ResultStatus WriteBlock(uint8 (&block)[BlockSize]) {
        ResultStatus status = i2c.WriteByteArray(SlaveAddress, RegisterBlockData, 1, block, BlockSize);
        if (status != ResultStatus::ok) {
            return status;
        }
        return WriteRegister(RegisterBlockChecksum, BlockChecksum(block));
    }


    // Checksum is 255 - (sum of the 32 block bytes modulo 256).
    static uint8 BlockChecksum(const uint8 (&block)[BlockSize]) {
        uint8 sum = 0;
        for (uint8 index = 0; index < BlockSize; index++) {
            sum = static_cast<uint8>(sum + block[index]);
        }
        return static_cast<uint8>(255 - sum);
    }


    static void PutBigEndian16(uint8 (&block)[BlockSize], uint8 offset, uint16 value) {
        block[offset] = static_cast<uint8>(value >> 8);
        block[offset + 1] = static_cast<uint8>(value & 0xFF);
    }


    static uint16 GetBigEndian16(const uint8 (&block)[BlockSize], uint8 offset) {
        return static_cast<uint16>((static_cast<uint16>(block[offset]) << 8) | block[offset + 1]);
    }
};
