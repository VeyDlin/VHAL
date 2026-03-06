#pragma once
#include <VHAL.h>


struct BQ34Z100Registers {
    // === Standard Register Addresses (read via direct I2C read) ===
    struct Address {
        static constexpr uint8 Control               = 0x00; // 16-bit, control subcommand interface
        static constexpr uint8 StateOfCharge          = 0x02; // 1 byte, %
        static constexpr uint8 MaxError               = 0x03; // 1 byte, %
        static constexpr uint8 RemainingCapacity      = 0x04; // 16-bit, mAh
        static constexpr uint8 FullChargeCapacity     = 0x06; // 16-bit, mAh
        static constexpr uint8 Voltage                = 0x08; // 16-bit, mV
        static constexpr uint8 AverageCurrent         = 0x0A; // 16-bit, mA (signed)
        static constexpr uint8 Temperature            = 0x0C; // 16-bit, 0.1K
        static constexpr uint8 Flags                  = 0x0E; // 16-bit
        static constexpr uint8 Current                = 0x10; // 16-bit, mA (signed)
        static constexpr uint8 FlagsB                 = 0x12; // 16-bit
        static constexpr uint8 AverageTimeToEmpty     = 0x18; // 16-bit, minutes
        static constexpr uint8 AverageTimeToFull      = 0x1A; // 16-bit, minutes
        static constexpr uint8 PassedCharge           = 0x1C; // 16-bit, mAh (signed)
        static constexpr uint8 DoD0Time               = 0x1E; // 16-bit, minutes
        static constexpr uint8 AvailableEnergy        = 0x24; // 16-bit, 10 mWh
        static constexpr uint8 AveragePower           = 0x26; // 16-bit, 10 mW
        static constexpr uint8 SerialNumber           = 0x28; // 16-bit
        static constexpr uint8 InternalTemperature    = 0x2A; // 16-bit, 0.1K
        static constexpr uint8 CycleCount             = 0x2C; // 16-bit
        static constexpr uint8 StateOfHealth          = 0x2E; // 16-bit, %
        static constexpr uint8 ChargeVoltage          = 0x30; // 16-bit, mV
        static constexpr uint8 ChargeCurrent          = 0x32; // 16-bit, mA
        static constexpr uint8 PackConfiguration      = 0x3A; // 16-bit
        static constexpr uint8 DesignCapacity         = 0x3C; // 16-bit, mAh
        static constexpr uint8 GridNumber             = 0x62; // 1 byte
        static constexpr uint8 LearnedStatus          = 0x63; // 1 byte
        static constexpr uint8 DoDAtEoC               = 0x64; // 16-bit
        static constexpr uint8 QStart                 = 0x66; // 16-bit
        static constexpr uint8 TrueFCC                = 0x6A; // 16-bit
        static constexpr uint8 StateTime              = 0x6C; // 16-bit
        static constexpr uint8 QMaxPassedQ            = 0x6E; // 16-bit
        static constexpr uint8 DoD0                   = 0x70; // 16-bit
        static constexpr uint8 QMaxDoD0               = 0x72; // 16-bit
        static constexpr uint8 QMaxTime               = 0x74; // 16-bit

        // Flash access registers
        static constexpr uint8 BlockDataControl       = 0x61;
        static constexpr uint8 DataFlashClass          = 0x3E;
        static constexpr uint8 DataFlashBlock          = 0x3F;
        static constexpr uint8 BlockData               = 0x40; // 32 bytes (0x40-0x5F)
        static constexpr uint8 BlockDataChecksum       = 0x60;
    };


    // === Control Subcommands (written to Address::Control) ===
    struct ControlCmd {
        static constexpr uint16 ControlStatus         = 0x0000;
        static constexpr uint16 DeviceType            = 0x0001;
        static constexpr uint16 FWVersion             = 0x0002;
        static constexpr uint16 HWVersion             = 0x0003;
        static constexpr uint16 ResetData             = 0x0005;
        static constexpr uint16 PrevMacWrite          = 0x0007;
        static constexpr uint16 ChemID                = 0x0008;
        static constexpr uint16 BoardOffset           = 0x0009;
        static constexpr uint16 CCOffset              = 0x000A;
        static constexpr uint16 CCOffsetSave          = 0x000B;
        static constexpr uint16 DFVersion             = 0x000C;
        static constexpr uint16 SetFullSleep          = 0x0010;
        static constexpr uint16 StaticChemChksum      = 0x0017;
        static constexpr uint16 Sealed                = 0x0020;
        static constexpr uint16 ITEnable              = 0x0021;
        static constexpr uint16 CalEnable             = 0x002D;
        static constexpr uint16 Reset                 = 0x0041;
        static constexpr uint16 ExitCal               = 0x0080;
        static constexpr uint16 EnterCal              = 0x0081;
        static constexpr uint16 OffsetCal             = 0x0082;
    };


    // === Unseal Keys ===
    struct UnsealKey {
        static constexpr uint16 First                 = 0x0414;
        static constexpr uint16 Second                = 0x3672;
    };


    // === Flags Register Bits ===
    struct FlagsBit {
        static constexpr uint16 OTC                   = 0x8000; // Overtemperature in charge
        static constexpr uint16 OTD                   = 0x4000; // Overtemperature in discharge
        static constexpr uint16 BATHI                 = 0x2000; // Battery high (overvoltage)
        static constexpr uint16 BATLOW                = 0x1000; // Battery low
        static constexpr uint16 CHG_INH               = 0x0800; // Charge inhibit
        static constexpr uint16 FC                    = 0x0200; // Full charge
        static constexpr uint16 CHG                   = 0x0100; // Allow charging
        static constexpr uint16 OCVTAKEN              = 0x0080; // OCV measurement taken
        static constexpr uint16 CF                    = 0x0010; // Condition flag
        static constexpr uint16 SOC1                  = 0x0004; // SOC threshold 1
        static constexpr uint16 SOCF                  = 0x0002; // SOC final threshold
        static constexpr uint16 DSG                   = 0x0001; // Discharging
    };


    // === Control Status Bits ===
    struct ControlStatusBit {
        static constexpr uint16 CALEN                 = 0x1000; // Calibration enabled
        static constexpr uint16 CCA                   = 0x0800; // CC calibration active
        static constexpr uint16 BCA                   = 0x0400; // Board calibration active
        static constexpr uint16 SS                    = 0x2000; // Sealed status
    };


    // === Flash Subclass IDs ===
    struct FlashSubclass {
        static constexpr uint8 ChargeTermination      = 36;
        static constexpr uint8 DataClass              = 48;
        static constexpr uint8 Discharge              = 49;
        static constexpr uint8 Registers              = 64;
        static constexpr uint8 PowerClass             = 68;
        static constexpr uint8 ITCfg                  = 80;
        static constexpr uint8 QMax                   = 82;
        static constexpr uint8 Chemistry              = 83;
        static constexpr uint8 CalibrationData        = 104; // 0x68
        static constexpr uint8 CurrentThresholds      = 107;
    };


    // === Device Info ===
    struct DeviceInfo {
        static constexpr uint8 SlaveAddress           = 0x55;
        static constexpr uint16 ExpectedDeviceType    = 0x0100;
    };
};
