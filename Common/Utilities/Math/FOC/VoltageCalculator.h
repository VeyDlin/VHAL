#pragma once
#include <VHAL.h>
#include <cmath>


template<typename T = float>
class VoltageCalculator {
private:
	struct {
		T dcBusVoltage = 0;			// DC-bus voltage (pu)
		T modulationVoltageA = 0;  	// Modulation voltage phase A (pu)
		T modulationVoltageB = 0;	// Modulation voltage phase B (pu)
		T modulationVoltageC = 0;	// Modulation voltage phase C (pu)
	} in;


public:
	bool outputOfPhase = true; 	// Out of Phase adjustment

	struct Out {
		T phaseA = 0;		// Phase voltage phase A (pu)
		T phaseB = 0;		// Phase voltage phase B (pu)
		T phaseC = 0;		// Phase voltage phase C (pu)
		T alpha = 0;		// Stationary d-axis phase voltage (pu)
		T beta = 0;  		// Stationary q-axis phase voltage (pu)
	} out;


	VoltageCalculator& Set(T dcBusVoltage, T modulationVoltageA, T modulationVoltageB, T modulationVoltageC) {
		in.dcBusVoltage = dcBusVoltage;
		in.modulationVoltageA = modulationVoltageA;
		in.modulationVoltageB = modulationVoltageB;
		in.modulationVoltageC = modulationVoltageC;
		return *this;
	}


	// outputOfPhase = true for the out of phase correction if
	//	* modulationVoltageA is out of phase with PWM1,
	//	* modulationVoltageB is out of phase with PWM3,
	//	* modulationVoltageC is out of phase with PWM5,
	// otherwise, set 0 if their phases are correct.
	VoltageCalculator& Resolve() {

		// Scale the incomming Modulation functions with the DC bus voltage value and calculate the 3 Phase voltages
		T dcBusVoltage = in.dcBusVoltage / T(3);
		out.phaseA = dcBusVoltage * ((in.modulationVoltageA * T(2)) - in.modulationVoltageB - in.modulationVoltageC);
		out.phaseB = dcBusVoltage * ((in.modulationVoltageB * T(2)) - in.modulationVoltageA - in.modulationVoltageC);
		out.phaseC = dcBusVoltage * ((in.modulationVoltageC * T(2)) - in.modulationVoltageB - in.modulationVoltageA);

		if(outputOfPhase == false) {
			out.phaseA = -out.phaseA;
			out.phaseB = -out.phaseB;
		}

		// Voltage transformation (a, b, c)  ->  (Alpha, Beta)
		using std::sqrt;
		static const T SQRT_1D3 = sqrt(T(1) / T(3));
		out.alpha = out.phaseA;
		out.beta = (out.phaseA + (out.phaseB * T(2))) * SQRT_1D3;

		return *this;
	}


	Out Get() {
		return out;
	}
};
