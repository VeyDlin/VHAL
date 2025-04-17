#pragma once
#include <BSP.h>
#include <algorithm>


struct RT9467Registers {
    // === Register Addresses ===
    struct Address {
        static constexpr uint8 DeviceId = 0x00;
        static constexpr uint8 ChgCtrl1 = 0x01;
        static constexpr uint8 ChgCtrl2 = 0x02;
        static constexpr uint8 ChgCtrl3 = 0x03;
        static constexpr uint8 ChgCtrl4 = 0x04;
        static constexpr uint8 ChgCtrl5 = 0x05;
        static constexpr uint8 ChgCtrl6 = 0x06;
        static constexpr uint8 ChgCtrl7 = 0x07;
        static constexpr uint8 ChgCtrl8 = 0x08;
        static constexpr uint8 ChgCtrl9 = 0x09;
        static constexpr uint8 ChgCtrl10 = 0x0A;
        static constexpr uint8 ChgCtrl11 = 0x0B;
        static constexpr uint8 ChgCtrl12 = 0x0C;
        static constexpr uint8 ChgCtrl13 = 0x0D;
        static constexpr uint8 ChgCtrl14 = 0x0E;
        static constexpr uint8 ChgCtrl15 = 0x0F;
        static constexpr uint8 ChgCtrl16 = 0x10;
        static constexpr uint8 ChgCtrl17 = 0x21;
        static constexpr uint8 ChgCtrl18 = 0x22;
        static constexpr uint8 ChgCtrl19 = 0x20;
        static constexpr uint8 ChgAdc = 0x11;
        static constexpr uint8 ChgStat = 0x2A;
        static constexpr uint8 ChgFault = 0x2B;
        static constexpr uint8 TsStatc = 0x2C;
        static constexpr uint8 AdcDataHigh = 0x27;
        static constexpr uint8 AdcDataLow = 0x28;
        static constexpr uint8 AdcDataOrigHigh = 0x2D;
        static constexpr uint8 AdcDataOrigLow = 0x2E;
        static constexpr uint8 ChgIrq1 = 0x2F;
        static constexpr uint8 ChgIrq2 = 0x30;
        static constexpr uint8 ChgIrq3 = 0x31;
        static constexpr uint8 ChgStatc = 0x2A;
        static constexpr uint8 DpdmIrq = 0x32;
        static constexpr uint8 ChgIrq1Ctrl = 0x3D;
        static constexpr uint8 ChgIrq2Ctrl = 0x3E;
        static constexpr uint8 ChgIrq3Ctrl = 0x3F;
        static constexpr uint8 DpdmIrqCtrl = 0x66;
    };


    // === ADC unit/offset ===
    struct AdcCalib {
        struct Unit {
            static constexpr uint32 VbusDiv5   = 25000;  
            static constexpr uint32 VbusDiv2   = 10000;  
            static constexpr uint32 Vbat       = 5000;   
            static constexpr uint32 Vsys       = 5000;   
            static constexpr uint32 Regn       = 5000;   
            static constexpr uint32 TsBat      = 25;     
            static constexpr uint32 Ibus       = 50000;  
            static constexpr uint32 Ibat       = 50000;  
            static constexpr uint32 TempJunc   = 2;      
        };
    
        struct Offset {
            static constexpr int32 VbusDiv5    = 0;    
            static constexpr int32 VbusDiv2    = 0;    
            static constexpr int32 Vbat        = 0;    
            static constexpr int32 Vsys        = 0;    
            static constexpr int32 Regn        = 0;    
            static constexpr int32 TsBat       = 0;    
            static constexpr int32 Ibus        = 0;    
            static constexpr int32 Ibat        = 0;    
            static constexpr int32 TempJunc    = -40;  
        };
    };


    // === Bit Shifts ===
    struct Shift {
        static constexpr uint8 Reset = 7;
        static constexpr uint8 OpaMode = 0;
        static constexpr uint8 HighZ = 2;
        static constexpr uint8 IrqPulse = 3;
    
        static constexpr uint8 ChargeEnable = 0;
        static constexpr uint8 CfoEnable = 1;
        static constexpr uint8 InputLimitSel = 2;
        static constexpr uint8 TerminationEnable = 4;
        static constexpr uint8 ShipEnable = 7;
    
        static constexpr uint8 Aicr = 2;
        static constexpr uint8 AicrEnable = 1;
        static constexpr uint8 InputLimitEnable = 0;
    
        static constexpr uint8 ChargeVoltage = 1;
        static constexpr uint8 BoostVoltage = 2;
    
        static constexpr uint8 Mivr = 1;
        static constexpr uint8 MivrEnable = 0;
    
        static constexpr uint8 Ichg = 2;
        static constexpr uint8 Vprec = 4;
        static constexpr uint8 Iprec = 0;
    
        static constexpr uint8 Ieoc = 4;
        static constexpr uint8 BoostOcp = 0;
    
        static constexpr uint8 TimerEnable = 1;
        static constexpr uint8 WatchdogTimer = 5;
    
        static constexpr uint8 WatchdogEnable = 7;
        static constexpr uint8 WatchdogReset = 0;
    
        static constexpr uint8 AiclMeasure = 7;
        static constexpr uint8 AiclThreshold = 0;
    
        static constexpr uint8 JeitaEnable = 4;
    
        static constexpr uint8 AdcInputSelect = 4;
        static constexpr uint8 AdcStart = 0;
    
        static constexpr uint8 UsbChgEnable = 7;
        static constexpr uint8 HvdcpDetect = 3;
        static constexpr uint8 UsbStatus = 0;
    
        static constexpr uint8 PumpxEnable = 7;
        static constexpr uint8 PumpxVersion = 6;
        static constexpr uint8 PumpxUpDn = 5;
        static constexpr uint8 PumpxDec = 0;
    
        static constexpr uint8 IrCompRes = 3;
        static constexpr uint8 IrCompClamp = 0;
    
        static constexpr uint8 AdcStat = 0;
        static constexpr uint8 ChgStatus = 6;
    
        static constexpr uint8 PowerReady = 7;
        static constexpr uint8 ChgMivr = 6;
        static constexpr uint8 ChgAicr = 5;
    
        static constexpr uint8 VbusOvp = 7;
    
        static constexpr uint8 AiclMeasured = 0;
        static constexpr uint8 WatchdogTimeout = 3;
        static constexpr uint8 SoftStartFinish = 4;
    
        static constexpr uint8 AdcDone = 0;
        static constexpr uint8 PumpxDone = 1;
    
        static constexpr uint8 Attach = 0;
    
        static constexpr uint8 PowerReadyMask = 7;
        static constexpr uint8 MivrMask = 6;
        static constexpr uint8 AicrMask = 5;
    
        static constexpr uint8 VbusOvpMask = 7;
    
        static constexpr uint8 TsHot = 7;
        static constexpr uint8 TsWarm = 6;
        static constexpr uint8 TsCool = 5;
        static constexpr uint8 TsCold = 4;
    
        static constexpr uint8 ChgOtp = 7;
        static constexpr uint8 ChgRvpm = 6;
        static constexpr uint8 AdpBadm = 5;
        static constexpr uint8 StatChanged = 2;
        static constexpr uint8 Fault = 1;
        static constexpr uint8 TsStatChanged = 0;
    
        static constexpr uint8 IeocMask = 7;
        static constexpr uint8 Terminated = 6;
        static constexpr uint8 SsFinish = 4;
    
        static constexpr uint8 BoostBatUv = 5;
    
        static constexpr uint8 Dcdtm = 7;
        static constexpr uint8 ChargeDetect = 6;
        static constexpr uint8 Hvdcp = 5;
        static constexpr uint8 Detach = 1;

        static constexpr uint8 FreqSel = 5;
        static constexpr uint8 FreqForce = 4;

        static constexpr uint8 VbatOv = 4;

        static constexpr uint8 OtgEn = 0;

        static constexpr uint8 OtgIlim = 0;
        static constexpr uint8 OtgVbat = 4;


    };


    // === Bit Masks ===
    struct Mask {
        static constexpr uint8 Reset              = 1 << Shift::Reset;
        static constexpr uint8 OpaMode            = 1 << Shift::OpaMode;
        static constexpr uint8 HighZ              = 1 << Shift::HighZ;
        static constexpr uint8 IrqPulse           = 1 << Shift::IrqPulse;
    
        static constexpr uint8 ChargeEnable       = 1 << Shift::ChargeEnable;
        static constexpr uint8 CfoEnable          = 1 << Shift::CfoEnable;
        static constexpr uint8 InputLimitSel      = 0x0C;
        static constexpr uint8 TerminationEnable  = 1 << Shift::TerminationEnable;
        static constexpr uint8 ShipEnable         = 1 << Shift::ShipEnable;
    
        static constexpr uint8 Aicr               = 0xFC;
        static constexpr uint8 AicrEnable         = 1 << Shift::AicrEnable;
        static constexpr uint8 InputLimitEnable   = 1 << Shift::InputLimitEnable;
    
        static constexpr uint8 ChargeVoltage      = 0xFE;
        static constexpr uint8 BoostVoltage       = 0xFC;
    
        static constexpr uint8 Mivr               = 0xFE;
        static constexpr uint8 MivrEnable         = 1 << Shift::MivrEnable;
    
        static constexpr uint8 Ichg               = 0xFC;
        static constexpr uint8 Vprec              = 0xF0;
        static constexpr uint8 Iprec              = 0x0F;
    
        static constexpr uint8 Ieoc               = 0xF0;
        static constexpr uint8 BoostOcp           = 0x07;
    
        static constexpr uint8 TimerEnable        = 1 << Shift::TimerEnable;
        static constexpr uint8 WatchdogTimer      = 0xE0;
        static constexpr uint8 WatchdogEnable     = 1 << Shift::WatchdogEnable;
        static constexpr uint8 WatchdogReset      = 1 << Shift::WatchdogReset;
    
        static constexpr uint8 AiclMeasure        = 1 << Shift::AiclMeasure;
        static constexpr uint8 AiclThreshold      = 0x07;
    
        static constexpr uint8 JeitaEnable        = 1 << Shift::JeitaEnable;
    
        static constexpr uint8 AdcInputSelect     = 0xF0;
        static constexpr uint8 AdcStart           = 1 << Shift::AdcStart;
    
        static constexpr uint8 UsbChgEnable       = 1 << Shift::UsbChgEnable;
        static constexpr uint8 Hvdcp              = 1 << Shift::HvdcpDetect;
        static constexpr uint8 UsbStatus          = 0x07;
    
        static constexpr uint8 PumpxEnable        = 1 << Shift::PumpxEnable;
        static constexpr uint8 PumpxVersion       = 1 << Shift::PumpxVersion;
        static constexpr uint8 PumpxUpDn          = 1 << Shift::PumpxUpDn;
        static constexpr uint8 PumpxDec           = 0x1F;
    
        static constexpr uint8 IrCompRes          = 0x38;
        static constexpr uint8 IrCompClamp        = 0x07;
    
        static constexpr uint8 AdcStat            = 1 << Shift::AdcStat;
        static constexpr uint8 ChgStatus          = 0xC0;
    
        static constexpr uint8 PowerReady         = 1 << Shift::PowerReady;
        static constexpr uint8 ChgMivr            = 1 << Shift::ChgMivr;
        static constexpr uint8 ChgAicr            = 1 << Shift::ChgAicr;
    
        static constexpr uint8 VbusOv             = 1 << Shift::VbusOvp;
    
        static constexpr uint8 AiclMeasured       = 1 << Shift::AiclMeasured;
        static constexpr uint8 WdtExpired         = 1 << Shift::WatchdogTimeout;
        static constexpr uint8 ChargeComplete     = 1 << Shift::SoftStartFinish;
    
        static constexpr uint8 AdcDone            = 1 << Shift::AdcDone;
        static constexpr uint8 PumpxDone          = 1 << Shift::PumpxDone;
    
        static constexpr uint8 Attach             = 1 << Shift::Attach;
    
        static constexpr uint8 TsHot              = 1 << Shift::TsHot;
        static constexpr uint8 TsWarm             = 1 << Shift::TsWarm;
        static constexpr uint8 TsCool             = 1 << Shift::TsCool;
        static constexpr uint8 TsCold             = 1 << Shift::TsCold;
    
        static constexpr uint8 ChgOtp             = 1 << Shift::ChgOtp;
        static constexpr uint8 ChgRvpm            = 1 << Shift::ChgRvpm;
        static constexpr uint8 AdpBadm            = 1 << Shift::AdpBadm;
        static constexpr uint8 StatChanged        = 1 << Shift::StatChanged;
        static constexpr uint8 Fault              = 1 << Shift::Fault;
        static constexpr uint8 TsStatChanged      = 1 << Shift::TsStatChanged;
    
        static constexpr uint8 IeocMask           = 1 << Shift::IeocMask;
        static constexpr uint8 Terminated         = 1 << Shift::Terminated;
        static constexpr uint8 SsFinish           = 1 << Shift::SsFinish;
    
        static constexpr uint8 BoostBatUv         = 1 << Shift::BoostBatUv;
    
        static constexpr uint8 Dcdtm              = 1 << Shift::Dcdtm;
        static constexpr uint8 ChargeDetect       = 1 << Shift::ChargeDetect;
        static constexpr uint8 HvdcpDetect        = 1 << Shift::Hvdcp;
        static constexpr uint8 Detach             = 1 << Shift::Detach;

        static constexpr uint8 FreqSel			  = 1 << Shift::FreqSel;
        static constexpr uint8 FreqForce		  = 1 << Shift::FreqForce;

        static constexpr uint8 VbatOv 			  = 1 << Shift::VbatOv;

        static constexpr uint8 OtgEn 			  = 1 << Shift::OtgEn;

        static constexpr uint8 OtgIlim 			  = 0x07;
        static constexpr uint8 OtgVbat			  = 0xF0;

    };


    // === Configuration Constants ===
    struct Config {
        struct Parameter {
            uint32 min;
            uint32 max;
            uint32 step;
            inline constexpr uint8 Encode(float value) const {
                return static_cast<uint8>((static_cast<uint32>(std::clamp(static_cast<uint32>(value * 1e6f), min, max)) - min) / step);
            }
        };

        static constexpr Parameter Ieoc        = {100000,  	850000,  	50000};
        static constexpr Parameter Mivr        = {3900000, 	13400000, 	100000};
        static constexpr Parameter Aicr        = {100000,  	3250000,  	50000};
        static constexpr Parameter Ichg        = {100000,  	5000000,  	100000};
        static constexpr Parameter ChargeVolt  = {3900000, 	4710000,  	10000};
        static constexpr Parameter BoostVolt   = {4425000, 	5825000,  	25000};
        static constexpr Parameter VpRec       = {2000000, 	3500000,  	100000};
        static constexpr Parameter IpRec       = {100000,  	850000,  	50000};
        static constexpr Parameter IrRes       = {0,       	175000,  	25000};
        static constexpr Parameter IrClamp     = {0,       	224000,  	32000};
        static constexpr Parameter Pep20Volt   = {5500000, 	14500000, 	500000};
        static constexpr Parameter AiclVth     = {4100000, 	4800000,  	100000};
        static constexpr Parameter OtgVbat 	   = {2300000, 	3800000, 	100000};
    };


    // === Device Info ===
    struct DeviceInfo {
        static constexpr uint8 SlaveAddress     = 0x5B;
        static constexpr uint8 DeviceIdExpected = 0x67;
        static constexpr uint8 VendorId         = 0x90;
        static constexpr uint8 ChipRevE1        = 0x01;
        static constexpr uint8 ChipRevE2        = 0x02;
        static constexpr uint8 ChipRevE3        = 0x03;
        static constexpr uint8 ChipRevE4        = 0x04;
    };
};
