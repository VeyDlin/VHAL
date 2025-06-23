#pragma once
#include "IClimateControl.h"
#include <Math/PID/PidController.h>


class PidClimateControl: public IClimateControl {
public:
    static constexpr float temperatureTolerance = 0.5f; // Temperature tolerance for hold detection


private:
    PidController<float> pidController;


public:
    PidClimateControl() {
        // Default PID coefficients for temperature control
        pidController.SetCoefficients({1.0f, 0.1f, 0.05f}); // P, I, D
        pidController.SetOuput({1.0f, -1.0f, false}); // Output range: -1 to 1
        pidController.SetIntegratorLimit({true, 0.5f, -0.5f}); // Prevent integral windup
    }
    

    void SetPidCoefficients(float p, float i, float d) {
        pidController.SetCoefficients({p, i, d});
    }
    

    void SetOutputLimits(float min, float max) {
        pidController.SetOuput({max, min, false});
    }
    

    void SetIntegratorLimit(bool enable, float limit) {
        pidController.SetIntegratorLimit({enable, limit, -limit});
    }
    

    void SetStabilizationDetection(float errorTolerance, uint32 timeMs) {
        pidController.SetStabilizedEvent({
            .enable = true,
            .errorMax = errorTolerance,
            .errorMin = -errorTolerance,
            .timeMs = timeMs,
            .onStabilized = [this](PidController<float>& pid) {
                if (!waitHold && onHold) {
                    onHold();
                }
            }
        });
    }
    

    virtual void Execute() override {
        while(true) {
            Sleep(timeStep);
            
            if(onUpdateState == nullptr || workMode != WorkMode::Auto) {
                continue;
            }
            
            // Get current temperature
            float currentTemp = onUpdateState(0); // Get current temp with no change
            
            // Update PID controller
            pidController.SetReference(holdTemperature);
            pidController.SetFeedback(currentTemp, 1000 / timeStep.count()); // Convert ms to Hz
            pidController.Resolve();
            
            // Get PID output
            float pidOutput = pidController.Get();
            
            // Apply control mode constraints
            float controlOutput = ApplyControlMode(pidOutput);
            
            // Apply output
            if (controlOutput != 0) {
                onUpdateState(controlOutput);
            }
            
            // Check if we've reached hold temperature
            OnHold(currentTemp);
        }
    }
    

private:
    float ApplyControlMode(float pidOutput) {
        switch (controlMode) {
            case ControlMode::Heating:
                // Only allow positive output (heating)
                return std::max(0.0f, pidOutput);
                
            case ControlMode::Cooling:
                // Only allow negative output (cooling)
                return std::min(0.0f, pidOutput);
                
            case ControlMode::HeatingCooling:
                // Allow both positive and negative output
                return pidOutput;
        }
        
        return 0;
    }
    

    void OnHold(float currentTemp) {
        if(!waitHold) {
            return;
        }
        
        bool wasWaitingHold = waitHold;
        
        switch (controlMode) {
            case ControlMode::Heating:
                waitHold = currentTemp < (holdTemperature - temperatureTolerance);
                break;
                
            case ControlMode::Cooling:
                waitHold = currentTemp > (holdTemperature + temperatureTolerance);
                break;
                
            case ControlMode::HeatingCooling:
                waitHold = (std::abs(currentTemp - holdTemperature) > temperatureTolerance);
                break;
        }
        
        // Trigger callback when temperature first reaches target
        if(wasWaitingHold && !waitHold && onHold) {
            onHold();
        }
    }
};
