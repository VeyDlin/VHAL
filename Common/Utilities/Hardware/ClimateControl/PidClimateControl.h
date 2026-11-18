#pragma once
#include "IClimateControl.h"
#include <Math/PID/PidController.h>


template <RealType Type = float>
class PidClimateControl: public IClimateControl<Type> {
	using Base = IClimateControl<Type>;
	using typename Base::WorkMode;
	using typename Base::ControlMode;
	using Base::timeStep;
	using Base::workMode;
	using Base::controlMode;
	using Base::holdTemperature;
	using Base::waitHold;
	using Base::NotifyHold;

public:
	using Base::onUpdateState;
	using Base::onHold;

    static constexpr Type temperatureTolerance = Type(0.5f);


private:
    PidController<Type> pidController;
    bool stabilizationEnabled = false;  // When true, the stabilization event is the only trigger for onHold


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
        stabilizationEnabled = true;
        pidController.SetStabilizedEvent({
            .enable = true,
            .errorMax = errorTolerance,
            .errorMin = -errorTolerance,
            .timeMs = Type(static_cast<int>(timeMs)),
            .onStabilized = [this](PidController<Type>&) {
                if (!waitHold) {
                    NotifyHold();
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
            // A zero period has no frequency; the PID then keeps its previous output.
            std::chrono::milliseconds::rep periodMs = timeStep.count();
            uint32 frequency = periodMs > 0 ? static_cast<uint32>(1000 / periodMs) : 0;
            pidController.SetFeedback(currentTemp, frequency);
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

        // With stabilization detection enabled, the stabilization callback is the single trigger.
        if (wasWaitingHold && !waitHold && !stabilizationEnabled) {
            NotifyHold();
        }
    }
};
