#pragma once
#include <VHAL.h>
#include "RT9467Registers.h"


using Address = RT9467Registers::Address;
using Shift = RT9467Registers::Shift;
using Mask = RT9467Registers::Mask;
using ConfigConstants = RT9467Registers::Config;
using DeviceInfo = RT9467Registers::DeviceInfo;



class RT9467 {
public:
    enum class ChargingStatus : uint8 {
        Ready = 0,
        Charging = 1,
        Done = 2,
        Fault = 3
    };

    enum class AdcMeasurement : uint8 {
        Vbus = 1,
        Vbat = 4,
        Ibus = 8,
        Ibat = 9,
        Vsys = 3,
        InternalRegulator = 11,
        ThermistorBattery = 6,
        TempJunction = 12
    };

    enum class InputCurrentLimitMode : uint8 {
        FixedLimit3_2A = 0,
        BasedOnChargerType = 1,
        ManualAverageInputLimit = 2,
        LowestAvailable = 3
    };

    struct FaultStatus {
        bool vbusOverVoltage;
        bool vbatOverVoltage;
        bool thermalShutdown;
        bool adapterBad;
    };

    struct ChargingStatusFlags {
        bool minimumInputVoltageRegulationActive;
        bool averageInputCurrentRegulationActive;
        bool powerReady;
    };

    struct IrqFlag {
        // IRQ1
        bool otp = false;
        bool vbusOverVoltage = false;
        bool adapterBad = false;
        bool statChanged = false;
        bool fault = false;
        bool thermalStateChanged = false;

        // IRQ2
        bool aiclMeasured = false;
        bool watchdogTimeout = false;
        bool chargeComplete = false;

        // IRQ3
        bool adcDone = false;
        bool pumpExpressDone = false;

        // DPDM
        bool dcdDetect = false;
        bool chargeDetect = false;
        bool hvdcpDetect = false;
        bool detach = false;
        bool attach = false;
    };

    struct IrqLines {
        // CHG_IRQ1
        bool thermalShutdown = false;
        bool adapterRemoved = false;
        bool adapterBad = false;
        bool chargingStatusChanged = false;
        bool faultOccurred = false;
        bool tsStateChanged = false;

        // CHG_IRQ2
        bool ieocReached = false;
        bool chargeTerminated = false;
        bool softStartFinished = false;
        bool aiclMeasured = false;

        // CHG_IRQ3
        bool boostUndervoltage = false;
        bool pumpExpressDone = false;
        bool adcDone = false;

        // DPDM_IRQ
        bool dcdDetect = false;
        bool chargeDetect = false;
        bool hvdcpDetect = false;
        bool detach = false;
        bool attach = false;
    };


private:
    AI2C *i2c = nullptr;


public:
    RT9467() { }
    RT9467(AI2C& _i2c): i2c(&_i2c) { }



    // ========== Charging control ==========

    ResultStatus EnableCharging(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl2, Mask::ChargeEnable, enable ? Mask::ChargeEnable : 0);
    }


    Result<bool> IsChargingEnabled() {
    	return CheckBitRegister(Address::ChgCtrl2, Mask::ChargeEnable);
    }


    ResultStatus SetChargeCurrent(float current) {
        uint8 regValue = ConfigConstants::Ichg.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl7, Mask::Ichg, regValue << Shift::Ichg);
    }


    ResultStatus SetInputCurrentLimit(float current) {
        uint8 regValue = ConfigConstants::Aicr.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl3, Mask::Aicr, regValue << Shift::Aicr);
    }


    ResultStatus SetChargeVoltage(float voltage) {
        uint8 regValue = ConfigConstants::ChargeVolt.Encode(voltage);
        return UpdateRegisterBits(Address::ChgCtrl4,Mask::ChargeVoltage, regValue << Shift::ChargeVoltage);
    }


    ResultStatus SetTerminationCurrent(float current) {
        uint8 regValue = ConfigConstants::Ieoc.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl9, Mask::Ieoc, regValue << Shift::Ieoc);
    }


    ResultStatus SetPrechargeThreshold(float voltage) {
        uint8 regValue = ConfigConstants::VpRec.Encode(voltage);
        return UpdateRegisterBits(Address::ChgCtrl8, Mask::Vprec, regValue << Shift::Vprec);
    }


    ResultStatus SetPrechargeCurrent(float current) {
        uint8 regValue = ConfigConstants::IpRec.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl8, Mask::Iprec, regValue << Shift::Iprec);
    }


    ResultStatus SetMinimumInputVoltage(float voltage) {
        uint8 regValue = ConfigConstants::Mivr.Encode(voltage);
        return UpdateRegisterBits( Address::ChgCtrl6, Mask::Mivr, regValue << Shift::Mivr);
    }


    ResultStatus SetIrCompensation(float resistance, float clampVoltage) {
        uint8 resReg = ConfigConstants::IrRes.Encode(resistance);
        uint8 clampReg = ConfigConstants::IrClamp.Encode(clampVoltage);
        return UpdateRegisterBits(
            Address::ChgCtrl18,
            Mask::IrCompRes | Mask::IrCompClamp,
            (resReg << Shift::IrCompRes) |
            (clampReg << Shift::IrCompClamp)
        );
    }


    ResultStatus SetInputCurrentLimitMode(InputCurrentLimitMode mode) {
        return UpdateRegisterBits(Address::ChgCtrl2, Mask::InputLimitSel, static_cast<uint8>(mode) << Shift::InputLimitSel);
    }




    // ========== Power path ==========

    ResultStatus SetHighImpedanceMode(bool enable) {
        return UpdateRegisterBits( Address::ChgCtrl1, Mask::HighZ, enable ? Mask::HighZ : 0);
    }


    Result<bool> IsInHighImpedanceMode() {
    	return CheckBitRegister(Address::ChgCtrl1, Mask::HighZ);
    }


    Result<bool> IsPowerReady() {
    	return CheckBitRegister(Address::ChgStatc, Mask::PowerReady);
    }




    // ========== Special modes ==========

    ResultStatus EnterShippingMode() {
        return UpdateRegisterBits(Address::ChgCtrl2, Mask::ShipEnable, Mask::ShipEnable);
    }


    ResultStatus Reset() {
        return UpdateRegisterBits(Address::DeviceId, Mask::Reset, Mask::Reset);
    }




    // ========== Timers & watchdog ==========

    ResultStatus EnableSafetyTimer(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl12, Mask::TimerEnable, enable ? Mask::TimerEnable : 0);
    }


    ResultStatus SetSafetyTimerHours(uint8 hours) {
        uint8 code = 0;
        if (hours >= 40) {
            code = 7;
        } else if (hours >= 20) {
            code = 6;
        } else if (hours >= 12) {
            code = 5;
        } else if (hours >= 8) {
            code = 4;
        } else if (hours >= 6) {
            code = 3;
        } else if (hours >= 4) {
            code = 2;
        } else if (hours >= 2) {
            code = 1;
        } else {
            code = 0;
        }

        return UpdateRegisterBits(Address::ChgCtrl12, Mask::TimerEnable, code << Shift::TimerEnable);
    }


    ResultStatus EnableWatchdog(bool enable, uint8 timeoutMin) {
        uint8 encodedTimeout = 0;
        switch (timeoutMin) {
            case 0: encodedTimeout = 0; break;
            case 5: encodedTimeout = 1; break;
            case 10: encodedTimeout = 2; break;
            case 20: encodedTimeout = 3; break;
            case 40: encodedTimeout = 4; break;
            case 80: encodedTimeout = 5; break;
            case 160: encodedTimeout = 6; break;
            default: encodedTimeout = 2; break; // default to 10 min
        }
        return UpdateRegisterBits(
            Address::ChgCtrl13,
            Mask::WatchdogEnable | Mask::WatchdogTimer,
            (enable ? Mask::WatchdogEnable : 0) |
            (encodedTimeout << Shift::WatchdogTimer)
        );
    }


    ResultStatus KickWatchdog() {
        return UpdateRegisterBits(Address::ChgCtrl13, Mask::WatchdogReset, Mask::WatchdogReset);
    }


    Result<bool> IsWatchdogExpired() {
    	return CheckBitRegister(Address::ChgIrq2, Mask::WdtExpired);
    }




    // ========== JEITA & thermistor ==========

    ResultStatus EnableJeita(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl16, Mask::JeitaEnable, enable ? Mask::JeitaEnable : 0);
    }


    Result<uint16> ReadBatteryTemperature() {
        auto read = ReadAdc(AdcMeasurement::ThermistorBattery);
        if (read.IsErr()) return read.Error();
        return read.Value() * 25; // 0.01% unit step, convert to raw JEITA
    }




    // ========== OTG mode ==========

    ResultStatus EnableOtg(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl2,Mask::OtgEn, enable ? Mask::OtgEn : 0);
    }


    ResultStatus SetOtgProtection(float current, float lowBatteryVoltage) {
    	uint8 ilim = 6;
        if (current < 0.6f) ilim = 0;
        if (current < 0.9f) ilim = 1;
        if (current < 1.2f) ilim = 2;
        if (current < 1.6f) ilim = 3;
        if (current < 2.0f) ilim = 4;
        if (current < 2.3f) ilim = 5;

        uint8 vbat = ConfigConstants::OtgVbat.Encode(lowBatteryVoltage);
        return UpdateRegisterBits(
            Address::ChgCtrl10,
            Mask::OtgIlim | Mask::OtgVbat,
            (ilim << Shift::OtgIlim) |
            (vbat << Shift::OtgVbat)
        );
    }




    // ========== QC/PE+/PD ==========

    ResultStatus EnablePumpExpress(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl17, Mask::PumpxEnable, enable ? Mask::PumpxEnable : 0);
    }




    // ========== Frequency ==========

    ResultStatus SetSwitchingFrequencyHigh(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl15, Mask::FreqSel, enable ? Mask::FreqSel : 0);
    }


    ResultStatus EnableFixedSwitchingFrequency(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl15, Mask::FreqForce, enable ? Mask::FreqForce : 0);
    }




    // ========== ADC ==========

    Result<uint16> ReadAdc(AdcMeasurement channel) {
        auto status = UpdateRegisterBits(
        	Address::ChgAdc,
    	    Mask::AdcInputSelect,
    	    static_cast<uint8>(channel) << Shift::AdcInputSelect
        );
        StatusAssert(status);

        status = UpdateRegisterBits(Address::ChgAdc, Mask::AdcStart, Mask::AdcStart);
        StatusAssert(status);

        for (uint8 i = 0; i < 6; ++i) {
            auto irq = ReadRegister(Address::ChgIrq3);
            if (irq.IsErr()) return irq.Error();

            if (irq.Value() & Mask::AdcDone) {
                break;
            }
            System::DelayMs(35);
        }

        System::DelayMs(1);

        auto msb = ReadRegister(Address::AdcDataHigh);
        if (msb.IsErr()) return msb.Error();

        auto lsb = ReadRegister(Address::AdcDataLow);
        if (lsb.IsErr()) return lsb.Error();

        return ((msb.Value() << 2) | (lsb.Value() >> 6));
    }




    // ========== Interrupts ==========

    ResultStatus SetInterrupt(const IrqLines& enable) {
        uint8 irq1 = 0, irq2 = 0, irq3 = 0, dpdm = 0;

        if (enable.thermalShutdown)       irq1 |= Mask::ChgOtp;
        if (enable.adapterRemoved)        irq1 |= Mask::ChgRvpm;
        if (enable.adapterBad)            irq1 |= Mask::AdpBadm;
        if (enable.chargingStatusChanged) irq1 |= Mask::StatChanged;
        if (enable.faultOccurred)         irq1 |= Mask::Fault;
        if (enable.tsStateChanged)        irq1 |= Mask::TsStatChanged;

        if (enable.ieocReached)           irq2 |= Mask::IeocMask;
        if (enable.chargeTerminated)      irq2 |= Mask::Terminated;
        if (enable.softStartFinished)     irq2 |= Mask::SsFinish;
        if (enable.aiclMeasured)          irq2 |= Mask::AiclMeasured;

        if (enable.boostUndervoltage)     irq3 |= Mask::BoostBatUv;
        if (enable.pumpExpressDone)       irq3 |= Mask::PumpxDone;
        if (enable.adcDone)               irq3 |= Mask::AdcDone;

        if (enable.dcdDetect)             dpdm |= Mask::Dcdtm;
        if (enable.chargeDetect)          dpdm |= Mask::ChargeDetect;
        if (enable.hvdcpDetect)           dpdm |= Mask::HvdcpDetect;
        if (enable.detach)                dpdm |= Mask::Detach;
        if (enable.attach)                dpdm |= Mask::Attach;

        auto status = UpdateRegisterBits(Address::ChgIrq1Ctrl, 0xFF, irq1);
        StatusAssert(status);

        status = UpdateRegisterBits(Address::ChgIrq2Ctrl, 0xFF, irq2);
        StatusAssert(status);

        status = UpdateRegisterBits(Address::ChgIrq3Ctrl, 0xFF, irq3);
        StatusAssert(status);

        status = UpdateRegisterBits(Address::DpdmIrqCtrl,  0xFF, dpdm);
        StatusAssert(status);

        return status;
    }


    Result<IrqLines> GetInterruptLines() {
        IrqLines flags{};

        auto irq1 = ReadRegister(Address::ChgIrq1);
        if (irq1.IsErr()) return irq1.Error();

        auto irq2 = ReadRegister(Address::ChgIrq2);
        if (irq2.IsErr()) return irq2.Error();

        auto irq3 = ReadRegister(Address::ChgIrq3);
        if (irq3.IsErr()) return irq3.Error();

        auto dpdm = ReadRegister(Address::DpdmIrq);
        if (dpdm.IsErr()) return dpdm.Error();

        flags.thermalShutdown       = irq1.Value() & Mask::ChgOtp;
        flags.adapterRemoved        = irq1.Value() & Mask::ChgRvpm;
        flags.adapterBad            = irq1.Value() & Mask::AdpBadm;
        flags.chargingStatusChanged = irq1.Value() & Mask::StatChanged;
        flags.faultOccurred         = irq1.Value() & Mask::Fault;
        flags.tsStateChanged        = irq1.Value() & Mask::TsStatChanged;

        flags.ieocReached           = irq2.Value() & Mask::IeocMask;
        flags.chargeTerminated      = irq2.Value() & Mask::Terminated;
        flags.softStartFinished     = irq2.Value() & Mask::SsFinish;
        flags.aiclMeasured          = irq2.Value() & Mask::AiclMeasured;

        flags.boostUndervoltage     = irq3.Value() & Mask::BoostBatUv;
        flags.pumpExpressDone       = irq3.Value() & Mask::PumpxDone;
        flags.adcDone               = irq3.Value() & Mask::AdcDone;

        flags.dcdDetect             = dpdm.Value() & Mask::Dcdtm;
        flags.chargeDetect          = dpdm.Value() & Mask::ChargeDetect;
        flags.hvdcpDetect           = dpdm.Value() & Mask::HvdcpDetect;
        flags.detach                = dpdm.Value() & Mask::Detach;
        flags.attach                = dpdm.Value() & Mask::Attach;

        return flags;
    }


    Result<IrqFlag> ReadInterruptFlags(bool readIrq1, bool readIrq2, bool readIrq3, bool readDpdm) {
        IrqFlag flags{};
        Result<uint8> irq1 = 0, irq2 = 0, irq3 = 0, dpdm = 0;

        if (readIrq1) {
        	irq1 = ReadRegister(Address::ChgIrq1);
        	if (irq1.IsErr()) return irq1.Error();
        }
        if (readIrq2) {
        	irq2 = ReadRegister(Address::ChgIrq2);
        	if (irq2.IsErr()) return irq2.Error();
        }
        if (readIrq3) {
        	irq3 = ReadRegister(Address::ChgIrq3);
        	if (irq3.IsErr()) return irq3.Error();
        }
        if (readDpdm) {
        	dpdm = ReadRegister(Address::DpdmIrq);
        	if (dpdm.IsErr()) return dpdm.Error();
        }

        flags.otp 					= irq1.Value() & Mask::ChgOtp;
        flags.vbusOverVoltage 		= irq1.Value() & Mask::VbusOv;
        flags.adapterBad 			= irq1.Value() & Mask::AdpBadm;
        flags.statChanged 			= irq1.Value() & Mask::StatChanged;
        flags.fault 				= irq1.Value() & Mask::Fault;
        flags.thermalStateChanged 	= irq1.Value() & Mask::TsStatChanged;

        flags.aiclMeasured 			= irq2.Value() & Mask::AiclMeasured;
        flags.watchdogTimeout 		= irq2.Value() & Mask::WdtExpired;
        flags.chargeComplete 		= irq2.Value() & Mask::ChargeComplete;

        flags.adcDone 				= irq3.Value() & Mask::AdcDone;
        flags.pumpExpressDone 		= irq3.Value() & Mask::PumpxDone;

        flags.dcdDetect 			= dpdm.Value() & Mask::Dcdtm;
        flags.chargeDetect 			= dpdm.Value() & Mask::ChargeDetect;
        flags.hvdcpDetect 			= dpdm.Value() & Mask::HvdcpDetect;
        flags.detach 				= dpdm.Value() & Mask::Detach;
        flags.attach 				= dpdm.Value() & Mask::Attach;

        return flags;
    }




    // ========== Status and faults ==========

    Result<ChargingStatus> GetChargingStatus() {
        auto read = ReadRegister(Address::ChgStatc);
        if (read.IsErr()) return read.Error();
        return static_cast<ChargingStatus>((read.Value() & Mask::ChgStatus) >> Shift::ChgStatus);
    }

    Result<FaultStatus> GetFaultStatus() {
        auto read = ReadRegister(Address::ChgFault);
        if (read.IsErr()) return read.Error();

        FaultStatus status{};
        status.vbusOverVoltage = read.Value() & Mask::VbusOv;
        status.vbatOverVoltage = read.Value() & Mask::VbatOv;
        status.thermalShutdown = read.Value() & Mask::ChgOtp;
        status.adapterBad = read.Value() & Mask::AdpBadm;
        return status;
    }


    Result<ChargingStatusFlags> GetChargingStatusFlags() {
        auto read = ReadRegister(Address::ChgStatc);
        if (read.IsErr()) return read.Error();

        ChargingStatusFlags flags{};
        flags.minimumInputVoltageRegulationActive = read.Value() & Mask::ChgMivr;
        flags.averageInputCurrentRegulationActive = read.Value() & Mask::ChgAicr;
        flags.powerReady = read.Value() & Mask::PowerReady;
        return flags;
    }




    // ========== Info ==========

    Result<uint8> GetDeviceId() {
        return ReadRegister(Address::DeviceId);
    }


    Result<uint8> GetChipRevision() {
        return ReadRegister(Address::ChgCtrl14);
    }


    Result<bool> IsPresent() {
    	auto id = GetDeviceId();
    	if (id.IsErr()) return id.Error();
        return id.Value() == DeviceInfo::DeviceIdExpected;
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
