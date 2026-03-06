#pragma once
#include <VHAL.h>
#include <algorithm>


struct BQ25798Registers {
    // === Register Addresses ===
    struct Address {
        // Configuration (8-bit unless noted)
        static constexpr uint8 MinSystemVoltage     = 0x00;
        static constexpr uint8 ChargeVoltageLimit   = 0x01; // 16-bit
        static constexpr uint8 ChargeCurrentLimit    = 0x03; // 16-bit
        static constexpr uint8 InputVoltageLimit     = 0x05;
        static constexpr uint8 InputCurrentLimit     = 0x06; // 16-bit
        static constexpr uint8 PrechargeControl      = 0x08;
        static constexpr uint8 TerminationControl    = 0x09;
        static constexpr uint8 RechargeControl       = 0x0A;
        static constexpr uint8 OtgVoltage            = 0x0B; // 16-bit
        static constexpr uint8 OtgRegulation         = 0x0D;
        static constexpr uint8 TimerControl          = 0x0E;
        static constexpr uint8 ChargerCtrl0          = 0x0F;
        static constexpr uint8 ChargerCtrl1          = 0x10;
        static constexpr uint8 ChargerCtrl2          = 0x11;
        static constexpr uint8 ChargerCtrl3          = 0x12;
        static constexpr uint8 ChargerCtrl4          = 0x13;
        static constexpr uint8 ChargerCtrl5          = 0x14;
        static constexpr uint8 MpptControl           = 0x15;
        static constexpr uint8 TemperatureControl    = 0x16;
        static constexpr uint8 NtcControl0           = 0x17;
        static constexpr uint8 NtcControl1           = 0x18;
        static constexpr uint8 IcoCurrentLimit       = 0x19; // 16-bit, read-only

        // Status (read-only)
        static constexpr uint8 ChargerStatus0        = 0x1B;
        static constexpr uint8 ChargerStatus1        = 0x1C;
        static constexpr uint8 ChargerStatus2        = 0x1D;
        static constexpr uint8 ChargerStatus3        = 0x1E;
        static constexpr uint8 ChargerStatus4        = 0x1F;
        static constexpr uint8 FaultStatus0          = 0x20;
        static constexpr uint8 FaultStatus1          = 0x21;

        // Flags (read to clear)
        static constexpr uint8 ChargerFlag0          = 0x22;
        static constexpr uint8 ChargerFlag1          = 0x23;
        static constexpr uint8 ChargerFlag2          = 0x24;
        static constexpr uint8 ChargerFlag3          = 0x25;
        static constexpr uint8 FaultFlag0            = 0x26;
        static constexpr uint8 FaultFlag1            = 0x27;

        // Masks
        static constexpr uint8 ChargerMask0          = 0x28;
        static constexpr uint8 ChargerMask1          = 0x29;
        static constexpr uint8 ChargerMask2          = 0x2A;
        static constexpr uint8 ChargerMask3          = 0x2B;
        static constexpr uint8 FaultMask0            = 0x2C;
        static constexpr uint8 FaultMask1            = 0x2D;

        // ADC
        static constexpr uint8 AdcControl            = 0x2E;
        static constexpr uint8 AdcFuncDisable0       = 0x2F;
        static constexpr uint8 AdcFuncDisable1       = 0x30;
        static constexpr uint8 IbusAdc               = 0x31; // 16-bit
        static constexpr uint8 IbatAdc               = 0x33; // 16-bit
        static constexpr uint8 VbusAdc               = 0x35; // 16-bit
        static constexpr uint8 Vac1Adc               = 0x37; // 16-bit
        static constexpr uint8 Vac2Adc               = 0x39; // 16-bit
        static constexpr uint8 VbatAdc               = 0x3B; // 16-bit
        static constexpr uint8 VsysAdc               = 0x3D; // 16-bit
        static constexpr uint8 TsAdc                 = 0x3F; // 16-bit
        static constexpr uint8 TdieAdc               = 0x41; // 16-bit
        static constexpr uint8 DplusAdc              = 0x43; // 16-bit
        static constexpr uint8 DminusAdc             = 0x45; // 16-bit

        // Misc
        static constexpr uint8 DpdmDriver            = 0x47;
        static constexpr uint8 PartInformation       = 0x48;
    };


    // === Bit Shifts ===
    struct Shift {
        // PrechargeControl (0x08)
        static constexpr uint8 VbatLowV         = 6;
        static constexpr uint8 Iprechg          = 0;

        // TerminationControl (0x09)
        static constexpr uint8 RegRst           = 6;
        static constexpr uint8 StopWdChgTmr     = 5;
        static constexpr uint8 Iterm            = 0;

        // RechargeControl (0x0A)
        static constexpr uint8 CellCount        = 6;
        static constexpr uint8 Trechg           = 4;
        static constexpr uint8 Vrechg           = 0;

        // OtgRegulation (0x0D)
        static constexpr uint8 PrechgTmr        = 7;
        static constexpr uint8 Iotg             = 0;

        // TimerControl (0x0E)
        static constexpr uint8 TopoffTmr        = 6;
        static constexpr uint8 EnTrickleTmr     = 5;
        static constexpr uint8 EnPrechgTmr      = 4;
        static constexpr uint8 EnChgTmr         = 3;
        static constexpr uint8 ChgTmr           = 1;
        static constexpr uint8 Tmr2xEn          = 0;

        // ChargerCtrl0 (0x0F)
        static constexpr uint8 AutoIbatdis      = 7;
        static constexpr uint8 ForceIbatdis     = 6;
        static constexpr uint8 EnChg            = 5;
        static constexpr uint8 EnIco            = 4;
        static constexpr uint8 ForceIco         = 3;
        static constexpr uint8 EnHiz            = 2;
        static constexpr uint8 EnTerm           = 1;
        static constexpr uint8 EnBackup         = 0;

        // ChargerCtrl1 (0x10)
        static constexpr uint8 VbusBackup       = 6;
        static constexpr uint8 VacOvp           = 4;
        static constexpr uint8 WdRst            = 3;
        static constexpr uint8 Watchdog         = 0;

        // ChargerCtrl2 (0x11)
        static constexpr uint8 ForceIndet       = 7;
        static constexpr uint8 AutoIndet        = 6;
        static constexpr uint8 En12V            = 5;
        static constexpr uint8 En9V             = 4;
        static constexpr uint8 HvdcpEn          = 3;
        static constexpr uint8 SdrvCtrl         = 1;
        static constexpr uint8 SdrvDly          = 0;

        // ChargerCtrl3 (0x12)
        static constexpr uint8 EnOtg            = 6;
        static constexpr uint8 PfmOtgDis        = 5;
        static constexpr uint8 PfmFwdDis        = 4;
        static constexpr uint8 WkupDly          = 3;

        // ChargerCtrl5 (0x14)
        static constexpr uint8 SfetPresent      = 7;
        static constexpr uint8 EnIbat           = 5;
        static constexpr uint8 IbatReg          = 3;

        // AdcControl (0x2E)
        static constexpr uint8 AdcEn            = 7;
        static constexpr uint8 AdcRate          = 6;
        static constexpr uint8 AdcSample        = 4;
        static constexpr uint8 AdcAvg           = 3;

        // ChargerStatus0 (0x1B)
        static constexpr uint8 IindpmStat       = 7;
        static constexpr uint8 VindpmStat       = 6;
        static constexpr uint8 WdStat           = 5;
        static constexpr uint8 PoorsrcStat      = 4;
        static constexpr uint8 PgStat           = 3;
        static constexpr uint8 Ac2Present       = 2;
        static constexpr uint8 Ac1Present       = 1;
        static constexpr uint8 VbusPresent      = 0;

        // ChargerStatus1 (0x1C)
        static constexpr uint8 ChgStat          = 5;
        static constexpr uint8 VbusStat         = 1;
        static constexpr uint8 Bc12Done         = 0;

        // ChargerStatus3 (0x1E)
        static constexpr uint8 AdcDone          = 5;

        // PartInformation (0x48)
        static constexpr uint8 PartNum          = 3;
        static constexpr uint8 DevRev           = 0;
    };


    // === Bit Masks ===
    struct Mask {
        // PrechargeControl (0x08)
        static constexpr uint8 VbatLowV         = 0xC0;
        static constexpr uint8 Iprechg          = 0x3F;

        // TerminationControl (0x09)
        static constexpr uint8 RegRst           = 1 << Shift::RegRst;
        static constexpr uint8 StopWdChgTmr     = 1 << Shift::StopWdChgTmr;
        static constexpr uint8 Iterm            = 0x1F;

        // RechargeControl (0x0A)
        static constexpr uint8 CellCount        = 0xC0;
        static constexpr uint8 Trechg           = 0x30;
        static constexpr uint8 Vrechg           = 0x0F;

        // OtgRegulation (0x0D)
        static constexpr uint8 PrechgTmr        = 1 << Shift::PrechgTmr;
        static constexpr uint8 Iotg             = 0x7F;

        // TimerControl (0x0E)
        static constexpr uint8 TopoffTmr        = 0xC0;
        static constexpr uint8 EnTrickleTmr     = 1 << Shift::EnTrickleTmr;
        static constexpr uint8 EnPrechgTmr      = 1 << Shift::EnPrechgTmr;
        static constexpr uint8 EnChgTmr         = 1 << Shift::EnChgTmr;
        static constexpr uint8 ChgTmr           = 0x06;
        static constexpr uint8 Tmr2xEn          = 1 << Shift::Tmr2xEn;

        // ChargerCtrl0 (0x0F)
        static constexpr uint8 AutoIbatdis      = 1 << Shift::AutoIbatdis;
        static constexpr uint8 ForceIbatdis     = 1 << Shift::ForceIbatdis;
        static constexpr uint8 EnChg            = 1 << Shift::EnChg;
        static constexpr uint8 EnIco            = 1 << Shift::EnIco;
        static constexpr uint8 ForceIco         = 1 << Shift::ForceIco;
        static constexpr uint8 EnHiz            = 1 << Shift::EnHiz;
        static constexpr uint8 EnTerm           = 1 << Shift::EnTerm;
        static constexpr uint8 EnBackup         = 1 << Shift::EnBackup;

        // ChargerCtrl1 (0x10)
        static constexpr uint8 VbusBackup       = 0xC0;
        static constexpr uint8 VacOvp           = 0x30;
        static constexpr uint8 WdRst            = 1 << Shift::WdRst;
        static constexpr uint8 Watchdog         = 0x07;

        // ChargerCtrl2 (0x11)
        static constexpr uint8 ForceIndet       = 1 << Shift::ForceIndet;
        static constexpr uint8 AutoIndet        = 1 << Shift::AutoIndet;
        static constexpr uint8 En12V            = 1 << Shift::En12V;
        static constexpr uint8 En9V             = 1 << Shift::En9V;
        static constexpr uint8 HvdcpEn          = 1 << Shift::HvdcpEn;
        static constexpr uint8 SdrvCtrl         = 0x06;
        static constexpr uint8 SdrvDly          = 1 << Shift::SdrvDly;

        // ChargerCtrl3 (0x12)
        static constexpr uint8 EnOtg            = 1 << Shift::EnOtg;
        static constexpr uint8 PfmOtgDis        = 1 << Shift::PfmOtgDis;
        static constexpr uint8 PfmFwdDis        = 1 << Shift::PfmFwdDis;
        static constexpr uint8 WkupDly          = 1 << Shift::WkupDly;

        // AdcControl (0x2E)
        static constexpr uint8 AdcEn            = 1 << Shift::AdcEn;
        static constexpr uint8 AdcRate          = 1 << Shift::AdcRate;
        static constexpr uint8 AdcSample        = 0x30;
        static constexpr uint8 AdcAvg           = 1 << Shift::AdcAvg;

        // ChargerStatus0 (0x1B)
        static constexpr uint8 IindpmStat       = 1 << Shift::IindpmStat;
        static constexpr uint8 VindpmStat       = 1 << Shift::VindpmStat;
        static constexpr uint8 WdStat           = 1 << Shift::WdStat;
        static constexpr uint8 PgStat           = 1 << Shift::PgStat;
        static constexpr uint8 VbusPresent      = 1 << Shift::VbusPresent;

        // ChargerStatus1 (0x1C)
        static constexpr uint8 ChgStat          = 0xE0;
        static constexpr uint8 VbusStat         = 0x1E;
        static constexpr uint8 Bc12Done         = 1 << Shift::Bc12Done;

        // ChargerStatus3 (0x1E)
        static constexpr uint8 AdcDone          = 1 << Shift::AdcDone;

        // FaultStatus0 (0x20)
        static constexpr uint8 VbusOvp          = 0x40;
        static constexpr uint8 VbatOvp          = 0x20;
        static constexpr uint8 IbusOcp          = 0x10;
        static constexpr uint8 IbatOcp          = 0x08;
        static constexpr uint8 ConvOcp          = 0x04;
        static constexpr uint8 Vac2Ovp          = 0x02;
        static constexpr uint8 Vac1Ovp          = 0x01;

        // FaultStatus1 (0x21)
        static constexpr uint8 VsysOvp          = 0x80;
        static constexpr uint8 VsysShort        = 0x40;
        static constexpr uint8 OtgUvp           = 0x10;
        static constexpr uint8 OtgOvp           = 0x08;
        static constexpr uint8 Tshut            = 0x01;

        // PartInformation (0x48)
        static constexpr uint8 PartNum          = 0x38;
        static constexpr uint8 DevRev           = 0x07;

        // MinSystemVoltage (0x00)
        static constexpr uint8 MinSysV          = 0x3F;

        // InputVoltageLimit (0x05)
        static constexpr uint8 Vindpm           = 0xFF;
    };


    // === Configuration Parameters ===
    struct Config {
        struct Parameter {
            uint32 min;   // µV or µA
            uint32 max;
            uint32 step;
            inline constexpr uint16 Encode(float value) const {
                auto uv = static_cast<uint32>(std::clamp(value * 1e6f, static_cast<float>(min), static_cast<float>(max)));
                return static_cast<uint16>((uv - min) / step);
            }
            inline constexpr float Decode(uint16 raw) const {
                return static_cast<float>(min + raw * step) / 1e6f;
            }
        };

        static constexpr Parameter MinSystemV       = {2500000,  16000000,  250000};
        static constexpr Parameter ChargeVoltage    = {0,        18800000,  10000};
        static constexpr Parameter ChargeCurrent    = {0,        5000000,   10000};
        static constexpr Parameter InputVoltage     = {0,        22000000,  100000};
        static constexpr Parameter InputCurrent     = {0,        3300000,   10000};
        static constexpr Parameter PrechargeCurrent = {0,        2000000,   40000};
        static constexpr Parameter TermCurrent      = {0,        1000000,   40000};
        static constexpr Parameter OtgVoltage       = {2800000,  22000000,  10000};
        static constexpr Parameter OtgCurrent       = {0,        3360000,   40000};
        static constexpr Parameter RechargeOffset   = {50000,    800000,    50000};
    };


    // === Device Info ===
    struct DeviceInfo {
        static constexpr uint8 SlaveAddress     = 0x6B;
        static constexpr uint8 PartNumExpected  = 0x03; // bits [5:3] = 011b
    };
};
