#pragma once
#include <Utilities/Math/Simulation/Angles.h>
#include <concepts>
#include <cstddef>


// Permanent-magnet synchronous motor in the amplitude-invariant dq frame. The states are the d and q currents and the electrical
// angle. The machine is a surface machine when Ld equals Lq. The equations are the dq model of a permanent-magnet machine
// (Krause, Wasynczuk, Sudhoff and Pekarek, Analysis of Electric Machinery and Drive Systems, the dq model in the amplitude-invariant
// form), written in the same form as the Pulsim documentation (https://lgili.github.io/Pulsim/0.10.0/motor-models/):
//
//   vd = Rs id + Ld d(id)/dt - we Lq iq
//   vq = Rs iq + Lq d(iq)/dt + we (Ld id + psi)
//   Te = 1.5 p (psi iq + (Ld - Lq) id iq)
//   we = p wm,  d(thetaE)/dt = we
//
// The voltage is the command of the block, or the voltage that an inverter applied to it. The rotor speed comes from the bus.
namespace Simulation {


template<std::floating_point T>
class Pmsm {
public:
	struct Parameters {
		T resistance = 0;          // stator resistance per phase, ohm
		T ld = 0;                  // d-axis inductance, H
		T lq = 0;                  // q-axis inductance, H
		T psi = 0;                 // peak flux linkage of the magnet, Wb
		std::size_t polePairs = 0; // p
	};


	struct Signals {
		T voltageD = 0;
		T voltageQ = 0;
		T currentD = 0;
		T currentQ = 0;
		T thetaE = 0;
		T torque = 0;
	};


	static constexpr std::size_t states = 3;
	static constexpr int stage = 0;


private:
	Parameters parameters;
	T commandD = 0;
	T commandQ = 0;
	Signals latched;


public:
	constexpr Pmsm() = default;


	constexpr explicit Pmsm(Parameters value) : parameters(value) {
	}


	// The voltage command in the dq frame, V.
	constexpr void SetVoltageCommand(T vd, T vq) {
		commandD = vd;
		commandQ = vq;
	}


	template<typename Bus>
	constexpr void Publish(const T* state, Bus& bus) const {
		bus.voltageD = commandD;
		bus.voltageQ = commandQ;
		bus.currentD = state[0];
		bus.currentQ = state[1];
		bus.thetaE = state[2];
		bus.torque = static_cast<T>(1.5) * static_cast<T>(parameters.polePairs)
			* (parameters.psi * state[1] + (parameters.ld - parameters.lq) * state[0] * state[1]);
	}


	template<typename Bus>
	constexpr void Derivative(const T* state, const Bus& bus, T* derivative) const {
		const T id = state[0];
		const T iq = state[1];
		const T electricalSpeed = static_cast<T>(parameters.polePairs) * bus.speed;

		derivative[0] = (bus.voltageD - parameters.resistance * id + electricalSpeed * parameters.lq * iq) / parameters.ld;
		derivative[1] = (bus.voltageQ - parameters.resistance * iq - electricalSpeed * (parameters.ld * id + parameters.psi)) / parameters.lq;
		derivative[2] = electricalSpeed;
	}


	constexpr void Initialize(T*) const {
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		latched.currentD = bus.currentD;
		latched.currentQ = bus.currentQ;
		latched.thetaE = bus.thetaE;
		latched.torque = bus.torque;
	}


	// The d and q currents, A, at the end of the last period.
	constexpr T CurrentD() const {
		return latched.currentD;
	}


	constexpr T CurrentQ() const {
		return latched.currentQ;
	}


	// The electrical angle, rad, wrapped to [0, 2 pi) by the caller when a sensor needs it.
	constexpr T ThetaE() const {
		return latched.thetaE;
	}


	// The electromagnetic torque, N m.
	constexpr T Torque() const {
		return latched.torque;
	}
};


}
