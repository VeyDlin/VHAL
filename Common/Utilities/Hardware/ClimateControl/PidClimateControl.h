#pragma once
#include "IClimateControl.h"
#include <Math/PID/PidController.h>


template <RealType Type = float>
class PidClimateControl: public IClimateControl<Type> {
	using Base = IClimateControl<Type>;
	using typename Base::WorkMode;
	using typename Base::ControlMode;
	using Base::timeStep;
	using Base::onUpdateState;
	using Base::workMode;
	using Base::controlMode;
	using Base::holdTemperature;
	using Base::waitHold;
	using Base::onHold;

public:
    static constexpr Type temperatureTolerance = Type(0.5f);


private:
    PidController<Type> pidController;


public:
    PidClimateControl() {
        pidController.SetCoefficients({Type(1.0f), Type(0.1f), Type(0.05f)});
        pidController.SetOuput({Type(1.0f), Type(-1.0f), false});
        pidController.SetIntegratorLimit({true, Type(0.5f), Type(-0.5f)});
    }


    void SetPidCoefficients(Type p, Type i, Type d) {
        pidController.SetCoefficients({p, i, d});
    }


    void SetOutputLimits(Type min, Type max) {
        pidController.SetOuput({max, min, false});
    }


    void SetIntegratorLimit(bool enable, Type limit) {
        pidController.SetIntegratorLimit({enable, limit, -limit});
    }


    void SetStabilizationDetection(Type errorTolerance, uint32 timeMs) {
        pidController.SetStabilizedEvent({
            .enable = true,
            .errorMax = errorTolerance,
            .errorMin = -errorTolerance,
            .timeMs = Type(static_cast<int>(timeMs)),
            .onStabilized = [this](PidController<Type>& pid) {
                if (!waitHold && onHold) {
                    onHold();
                }
            }
        });
    }


    virtual void Execute() override {
        while(true) {
            this->Sleep(timeStep);

            if(onUpdateState == nullptr || workMode != WorkMode::Auto) {
                continue;
            }

            Type currentTemp = onUpdateState(Type(0));

            pidController.SetReference(holdTemperature);
            pidController.SetFeedback(currentTemp, 1000 / timeStep.count());
            pidController.Resolve();

            Type pidOutput = pidController.Get();
            Type controlOutput = ApplyControlMode(pidOutput);

            if (controlOutput != Type(0)) {
                onUpdateState(controlOutput);
            }

            OnHold(currentTemp);
        }
    }


private:
    Type ApplyControlMode(Type pidOutput) {
        using std::max;
        using std::min;
        switch (controlMode) {
            case ControlMode::Heating:
                return max(Type(0), pidOutput);

            case ControlMode::Cooling:
                return min(Type(0), pidOutput);

            case ControlMode::HeatingCooling:
                return pidOutput;
        }

        return Type(0);
    }


    void OnHold(Type currentTemp) {
        if(!waitHold) {
            return;
        }

        bool wasWaitingHold = waitHold;

        using std::abs;
        switch (controlMode) {
            case ControlMode::Heating:
                waitHold = currentTemp < (holdTemperature - temperatureTolerance);
                break;

            case ControlMode::Cooling:
                waitHold = currentTemp > (holdTemperature + temperatureTolerance);
                break;

            case ControlMode::HeatingCooling:
                waitHold = (abs(currentTemp - holdTemperature) > temperatureTolerance);
                break;
        }

        if(wasWaitingHold && !waitHold && onHold) {
            onHold();
        }
    }
};
