#pragma once

#include <VHAL.h>


class MP2664 {
public:
    static constexpr uint8 SlaveAddress = 0x09;

    enum class ChargingState : uint8 {
        NotCharging = 0,
        PreCharge = 1,
        Charging = 2,
        Done = 3
    };

    struct Status {
        uint8 revision = 0;
        ChargingState charging = ChargingState::NotCharging;
        bool powerPathManagement = false;
        bool powerGood = false;
        bool thermalRegulation = false;
        bool watchdogFault = false;
        bool inputFault = false;
        bool thermalShutdown = false;
        bool batteryFault = false;
        bool safetyTimerFault = false;
        bool ntcHot = false;
        bool ntcCold = false;
    };

private:
    AI2C& i2c;

public:
    explicit MP2664(AI2C& bus): i2c(bus) { }


    Result<Status> ReadStatus() {
        Result<uint8> system = ReadRegister(0x07);
        if (system.IsErr()) {
            return system.Error();
        }
        Result<uint8> fault = ReadRegister(0x08);
        if (fault.IsErr()) {
            return fault.Error();
        }
        Status status{};
        status.revision = static_cast<uint8>((system.Value() >> 5) & 0x03);
        status.charging = static_cast<ChargingState>((system.Value() >> 3) & 0x03);
        status.powerPathManagement = (system.Value() & 0x04) != 0;
        status.powerGood = (system.Value() & 0x02) != 0;
        status.thermalRegulation = (system.Value() & 0x01) != 0;
        status.watchdogFault = (fault.Value() & 0x40) != 0;
        status.inputFault = (fault.Value() & 0x20) != 0;
        status.thermalShutdown = (fault.Value() & 0x10) != 0;
        status.batteryFault = (fault.Value() & 0x08) != 0;
        status.safetyTimerFault = (fault.Value() & 0x04) != 0;
        status.ntcHot = (fault.Value() & 0x02) != 0;
        status.ntcCold = (fault.Value() & 0x01) != 0;
        return status;
    }


    ResultStatus SetChargingEnabled(bool enabled) {
        Result<uint8> current = ReadRegister(0x01);
        if (current.IsErr()) {
            return current.Error();
        }
        uint8 value = static_cast<uint8>(current.Value() & 0x3F);
        if (enabled) {
            value = static_cast<uint8>(value & static_cast<uint8>(~0x08));
        } else {
            value = static_cast<uint8>(value | 0x08);
        }
        return WriteRegister(0x01, value);
    }


    ResultStatus SetInputCurrentLimitMa(uint16 currentMa) {
        static constexpr uint16 Values[8] = {85, 130, 175, 220, 265, 310, 355, 455};
        uint8 code = 0;
        bool found = false;
        for (uint8 index = 0; index < 8; index++) {
            if (Values[index] == currentMa) {
                code = index;
                found = true;
                break;
            }
        }
        if (!found) {
            return ResultStatus::outOfRange;
        }
        return UpdateRegisterBits(0x00, 0x07, code);
    }


    ResultStatus SetChargeCurrentMa(uint16 currentMa) {
        if (currentMa < 8 || currentMa > 535 || (currentMa - 8) % 17 != 0) {
            return ResultStatus::outOfRange;
        }
        uint8 code = static_cast<uint8>((currentMa - 8) / 17);
        if (code > 31) {
            return ResultStatus::outOfRange;
        }
        return UpdateRegisterBits(0x02, 0x1F, code);
    }

private:
    Result<uint8> ReadRegister(uint8 address) {
        uint8 data = 0;
        ResultStatus result = i2c.ReadByteArray(SlaveAddress, address, 1, &data, 1);
        if (result != ResultStatus::ok) {
            return result;
        }
        return data;
    }


    ResultStatus WriteRegister(uint8 address, uint8 data) {
        return i2c.WriteByteArray(SlaveAddress, address, 1, &data, 1);
    }


    ResultStatus UpdateRegisterBits(uint8 address, uint8 mask, uint8 bits) {
        Result<uint8> current = ReadRegister(address);
        if (current.IsErr()) {
            return current.Error();
        }
        uint8 value = static_cast<uint8>((current.Value() & static_cast<uint8>(~mask)) | (bits & mask));
        return WriteRegister(address, value);
    }
};
