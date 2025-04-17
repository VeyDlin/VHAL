#pragma once
#include <BSP.h>
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

    Status::statusType EnableCharging(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl2, Mask::ChargeEnable, enable ? Mask::ChargeEnable : 0);
    }


    Status::info<bool> IsChargingEnabled() {
    	return CheckBitRegister(Address::ChgCtrl2, Mask::ChargeEnable);
    }


    Status::statusType SetChargeCurrent(float current) {
        uint8 regValue = ConfigConstants::Ichg.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl7, Mask::Ichg, regValue << Shift::Ichg);
    }


    Status::statusType SetInputCurrentLimit(float current) {
        uint8 regValue = ConfigConstants::Aicr.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl3, Mask::Aicr, regValue << Shift::Aicr);
    }


    Status::statusType SetChargeVoltage(float voltage) {
        uint8 regValue = ConfigConstants::ChargeVolt.Encode(voltage);
        return UpdateRegisterBits(Address::ChgCtrl4,Mask::ChargeVoltage, regValue << Shift::ChargeVoltage);
    }


    Status::statusType SetTerminationCurrent(float current) {
        uint8 regValue = ConfigConstants::Ieoc.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl9, Mask::Ieoc, regValue << Shift::Ieoc);
    }


    Status::statusType SetPrechargeThreshold(float voltage) {
        uint8 regValue = ConfigConstants::VpRec.Encode(voltage);
        return UpdateRegisterBits(Address::ChgCtrl8, Mask::Vprec, regValue << Shift::Vprec);
    }


    Status::statusType SetPrechargeCurrent(float current) {
        uint8 regValue = ConfigConstants::IpRec.Encode(current);
        return UpdateRegisterBits(Address::ChgCtrl8, Mask::Iprec, regValue << Shift::Iprec);
    }


    Status::statusType SetMinimumInputVoltage(float voltage) {
        uint8 regValue = ConfigConstants::Mivr.Encode(voltage);
        return UpdateRegisterBits( Address::ChgCtrl6, Mask::Mivr, regValue << Shift::Mivr);
    }


    Status::statusType SetIrCompensation(float resistance, float clampVoltage) {
        uint8 resReg = ConfigConstants::IrRes.Encode(resistance);
        uint8 clampReg = ConfigConstants::IrClamp.Encode(clampVoltage);
        return UpdateRegisterBits(
            Address::ChgCtrl18,
            Mask::IrCompRes | Mask::IrCompClamp,
            (resReg << Shift::IrCompRes) |
            (clampReg << Shift::IrCompClamp)
        );
    }


    Status::statusType SetInputCurrentLimitMode(InputCurrentLimitMode mode) {
        return UpdateRegisterBits(Address::ChgCtrl2, Mask::InputLimitSel, static_cast<uint8>(mode) << Shift::InputLimitSel);
    }




    // ========== Power path ==========

    Status::statusType SetHighImpedanceMode(bool enable) {
        return UpdateRegisterBits( Address::ChgCtrl1, Mask::HighZ, enable ? Mask::HighZ : 0);
    }


    Status::info<bool> IsInHighImpedanceMode() {
    	return CheckBitRegister(Address::ChgCtrl1, Mask::HighZ);
    }


    Status::info<bool> IsPowerReady() {
    	return CheckBitRegister(Address::ChgStatc, Mask::PowerReady);
    }




    // ========== Special modes ==========

    Status::statusType EnterShippingMode() {
        return UpdateRegisterBits(Address::ChgCtrl2, Mask::ShipEnable, Mask::ShipEnable);
    }


    Status::statusType Reset() {
        return UpdateRegisterBits(Address::DeviceId, Mask::Reset, Mask::Reset);
    }




    // ========== Timers & watchdog ==========

    Status::statusType EnableSafetyTimer(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl12, Mask::TimerEnable, enable ? Mask::TimerEnable : 0);
    }


    Status::statusType SetSafetyTimerHours(uint8 hours) {
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


    Status::statusType EnableWatchdog(bool enable, uint8 timeoutMin) {
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


    Status::statusType KickWatchdog() {
        return UpdateRegisterBits(Address::ChgCtrl13, Mask::WatchdogReset, Mask::WatchdogReset);
    }


    Status::info<bool> IsWatchdogExpired() {
    	return CheckBitRegister(Address::ChgIrq2, Mask::WdtExpired);
    }




    // ========== JEITA & thermistor ==========

    Status::statusType EnableJeita(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl16, Mask::JeitaEnable, enable ? Mask::JeitaEnable : 0);
    }


    Status::info<uint16> ReadBatteryTemperature() {
        auto read = ReadAdc(AdcMeasurement::ThermistorBattery);
        StatusAssert(read.type);
        return read.data * 25; // 0.01% unit step, convert to raw JEITA
    }




    // ========== OTG mode ==========

    Status::statusType EnableOtg(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl2,Mask::OtgEn, enable ? Mask::OtgEn : 0);
    }


    Status::statusType SetOtgProtection(float current, float lowBatteryVoltage) {
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

    Status::statusType EnablePumpExpress(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl17, Mask::PumpxEnable, enable ? Mask::PumpxEnable : 0);
    }




    // ========== Frequency ==========

    Status::statusType SetSwitchingFrequencyHigh(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl15, Mask::FreqSel, enable ? Mask::FreqSel : 0);
    }


    Status::statusType EnableFixedSwitchingFrequency(bool enable) {
        return UpdateRegisterBits(Address::ChgCtrl15, Mask::FreqForce, enable ? Mask::FreqForce : 0);
    }




    // ========== ADC ==========

    Status::info<uint16> ReadAdc(AdcMeasurement channel) {
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
            StatusAssert(irq.type);

            if (irq.data & Mask::AdcDone) {
                break;
            }
            System::DelayMs(35);
        }

        System::DelayMs(1);

        auto msb = ReadRegister(Address::AdcDataHigh);
        StatusAssert(msb.type);

        auto lsb = ReadRegister(Address::AdcDataLow);
        StatusAssert(lsb.type);

        return ((msb.data << 2) | (lsb.data >> 6));
    }




    // ========== Interrupts ==========

    Status::statusType SetInterrupt(const IrqLines& enable) {
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


    Status::info<IrqLines> GetInterruptLines() {
        IrqLines flags{};

        auto irq1 = ReadRegister(Address::ChgIrq1);
        StatusAssert(irq1.type);

        auto irq2 = ReadRegister(Address::ChgIrq2);
        StatusAssert(irq2.type);

        auto irq3 = ReadRegister(Address::ChgIrq3);
        StatusAssert(irq3.type);

        auto dpdm = ReadRegister(Address::DpdmIrq);
        StatusAssert(dpdm.type);

        flags.thermalShutdown       = irq1.data & Mask::ChgOtp;
        flags.adapterRemoved        = irq1.data & Mask::ChgRvpm;
        flags.adapterBad            = irq1.data & Mask::AdpBadm;
        flags.chargingStatusChanged = irq1.data & Mask::StatChanged;
        flags.faultOccurred         = irq1.data & Mask::Fault;
        flags.tsStateChanged        = irq1.data & Mask::TsStatChanged;

        flags.ieocReached           = irq2.data & Mask::IeocMask;
        flags.chargeTerminated      = irq2.data & Mask::Terminated;
        flags.softStartFinished     = irq2.data & Mask::SsFinish;
        flags.aiclMeasured          = irq2.data & Mask::AiclMeasured;

        flags.boostUndervoltage     = irq3.data & Mask::BoostBatUv;
        flags.pumpExpressDone       = irq3.data & Mask::PumpxDone;
        flags.adcDone               = irq3.data & Mask::AdcDone;

        flags.dcdDetect             = dpdm.data & Mask::Dcdtm;
        flags.chargeDetect          = dpdm.data & Mask::ChargeDetect;
        flags.hvdcpDetect           = dpdm.data & Mask::HvdcpDetect;
        flags.detach                = dpdm.data & Mask::Detach;
        flags.attach                = dpdm.data & Mask::Attach;

        return flags;
    }


    Status::info<IrqFlag> ReadInterruptFlags(bool readIrq1, bool readIrq2, bool readIrq3, bool readDpdm) {
        IrqFlag flags{};
        Status::info<uint8> irq1 = 0, irq2 = 0, irq3 = 0, dpdm = 0;

        if (readIrq1) {
        	irq1 = ReadRegister(Address::ChgIrq1);
        	StatusAssert(irq1.type);
        }
        if (readIrq2) {
        	irq2 = ReadRegister(Address::ChgIrq2);
        	StatusAssert(irq2.type);
        }
        if (readIrq3) {
        	irq3 = ReadRegister(Address::ChgIrq3);
        	StatusAssert(irq3.type);
        }
        if (readDpdm) {
        	dpdm = ReadRegister(Address::DpdmIrq);
        	StatusAssert(dpdm.type);
        }

        flags.otp 					= irq1.data & Mask::ChgOtp;
        flags.vbusOverVoltage 		= irq1.data & Mask::VbusOv;
        flags.adapterBad 			= irq1.data & Mask::AdpBadm;
        flags.statChanged 			= irq1.data & Mask::StatChanged;
        flags.fault 				= irq1.data & Mask::Fault;
        flags.thermalStateChanged 	= irq1.data & Mask::TsStatChanged;

        flags.aiclMeasured 			= irq2.data & Mask::AiclMeasured;
        flags.watchdogTimeout 		= irq2.data & Mask::WdtExpired;
        flags.chargeComplete 		= irq2.data & Mask::ChargeComplete;

        flags.adcDone 				= irq3.data & Mask::AdcDone;
        flags.pumpExpressDone 		= irq3.data & Mask::PumpxDone;

        flags.dcdDetect 			= dpdm.data & Mask::Dcdtm;
        flags.chargeDetect 			= dpdm.data & Mask::ChargeDetect;
        flags.hvdcpDetect 			= dpdm.data & Mask::HvdcpDetect;
        flags.detach 				= dpdm.data & Mask::Detach;
        flags.attach 				= dpdm.data & Mask::Attach;

        return flags;
    }




    // ========== Status and faults ==========

    Status::info<ChargingStatus> GetChargingStatus() {
        auto read = ReadRegister(Address::ChgStatc);
        StatusAssert(read.type);
        return static_cast<ChargingStatus>((read.data & Mask::ChgStatus) >> Shift::ChgStatus);
    }

    Status::info<FaultStatus> GetFaultStatus() {
        auto read = ReadRegister(Address::ChgFault);
        StatusAssert(read.type);

        FaultStatus status{};
        status.vbusOverVoltage = read.data & Mask::VbusOv;
        status.vbatOverVoltage = read.data & Mask::VbatOv;
        status.thermalShutdown = read.data & Mask::ChgOtp;
        status.adapterBad = read.data & Mask::AdpBadm;
        return status;
    }


    Status::info<ChargingStatusFlags> GetChargingStatusFlags() {
        auto read = ReadRegister(Address::ChgStatc);
        StatusAssert(read.type);

        ChargingStatusFlags flags{};
        flags.minimumInputVoltageRegulationActive = read.data & Mask::ChgMivr;
        flags.averageInputCurrentRegulationActive = read.data & Mask::ChgAicr;
        flags.powerReady = read.data & Mask::PowerReady;
        return flags;
    }




    // ========== Info ==========

    Status::info<uint8> GetDeviceId() {
        return ReadRegister(Address::DeviceId);
    }


    Status::info<uint8> GetChipRevision() {
        return ReadRegister(Address::ChgCtrl14);
    }


    Status::info<bool> IsPresent() {
    	auto id = GetDeviceId();
    	StatusAssert(id.type);
        return id.data == DeviceInfo::DeviceIdExpected;
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
