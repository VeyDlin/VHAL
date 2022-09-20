#pragma once
#include <algorithm>
#include <cmath>


class SpeedCalculator {
private:
    float electricalAngle = 0;
    float oldElectricalAngle = 0;

public:
    uint32 baseSpeedRpm = 0; 	// Base speed in rpm
    float K1 = 0;       		// Constant for differentiator
    float K2 = 0;       		// Constant for low-pass filter (pu)
    float K3 = 0;       		// Constant for low-pass filter (pu)


    struct Out {
    	int32 speedRpm = 0; // Speed in rpm
        float speed = 0;    // Speed in per-unit (pu)
    } out;


    SpeedCalculator& Set(float angle) {
    	electricalAngle = angle;
    	return *this;
    }


    SpeedCalculator& Resolve() {
		// === Differentiator

		// Synchronous speed computation
    	float speed;
		if ((electricalAngle < 0.9) & (electricalAngle > 0.1)) {
			speed = K1 * (electricalAngle - oldElectricalAngle);
		} else {
			speed = out.speed;
		}


		// === Low-pass filter

		speed = (K2 * out.speed) + (K3 * speed);

		// Saturate the output
		out.speed = std::min(std::max(speed, -1.0f), 1.0f);

		// Update the electrical angle
		oldElectricalAngle = electricalAngle;

		// Change motor speed from pu value to rpm value
		out.speedRpm = baseSpeedRpm * out.speed;

		return *this;
	}


    Out Get() {
    	return out;
    }
};


