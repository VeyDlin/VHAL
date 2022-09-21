#pragma once
#include <BSP.h>
#include <cmath>

// TODO: Ref: volt_calc.h

class VoltageCalculator {
private:
	struct {
		float dcBusVoltage = 0;			// DC-bus voltage (pu)
		float modulationVoltageA = 0;  	// Modulation voltage phase A (pu)
		float modulationVoltageB = 0;	// Modulation voltage phase B (pu)
		float modulationVoltageC = 0;	// Modulation voltage phase C (pu)
	} in;


public:
	bool outputOfPhase = true; 	// Out of Phase adjustment

	struct Out {
		float phaseA = 0;		// Phase voltage phase A (pu)
		float phaseB = 0;		// Phase voltage phase B (pu)
		float phaseC = 0;		// Phase voltage phase C (pu)
		float alpha = 0;		// Stationary d-axis phase voltage (pu)
		float beta = 0;  		// Stationary q-axis phase voltage (pu)
	} out;


	VoltageCalculator& Set(float dcBusVoltage, float modulationVoltageA, float modulationVoltageB, float modulationVoltageC) {
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
		float dcBusVoltage = in.dcBusVoltage / 3;
		out.phaseA = dcBusVoltage * ((in.modulationVoltageA * 2) - in.modulationVoltageB - in.modulationVoltageC);
		out.phaseB = dcBusVoltage * ((in.modulationVoltageB * 2) - in.modulationVoltageA - in.modulationVoltageC);
		out.phaseC = dcBusVoltage * ((in.modulationVoltageC * 2) - in.modulationVoltageB - in.modulationVoltageA);	

		if(outputOfPhase == false) {
			out.phaseA = -out.phaseA;
			out.phaseB = -out.phaseB;
		}

		// Voltage transformation (a, b, c)  ->  (Alpha, Beta)
		static const float SQRT_1D3 = std::sqrt(1.0f / 3.0f);
		out.alpha = out.phaseA;
		out.beta = (out.phaseA + (out.phaseB * 2)) * SQRT_1D3;
						
		return *this;
	}


	Out Get() {
		return out;
	}
};





























