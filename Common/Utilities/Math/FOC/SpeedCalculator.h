#pragma once
#include <algorithm>
#include <cmath>


template<typename T = float>
class SpeedCalculator {
private:
    T electricalAngle = 0;
    T oldElectricalAngle = 0;

public:
    uint32 baseSpeedRpm = 0; 	// Base speed in rpm
    T K1 = 0;       		// Constant for differentiator
    T K2 = 0;       		// Constant for low-pass filter (pu)
    T K3 = 0;       		// Constant for low-pass filter (pu)


    struct Out {
    	int32 speedRpm = 0; // Speed in rpm
        T speed = 0;    // Speed in per-unit (pu)
    } out;


    SpeedCalculator& Set(T angle) {
    	electricalAngle = angle;
    	return *this;
    }


    SpeedCalculator& Resolve() {
		// === Differentiator

		// Synchronous speed computation
    	T speed;
		if ((electricalAngle < T(0.9)) & (electricalAngle > T(0.1))) {
			speed = K1 * (electricalAngle - oldElectricalAngle);
		} else {
			speed = out.speed;
		}


		// === Low-pass filter

		speed = (K2 * out.speed) + (K3 * speed);

		// Saturate the output
		using std::min;
		using std::max;
		out.speed = min(max(speed, T(-1)), T(1));

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
