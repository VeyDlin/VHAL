#pragma once
#include <cmath>
#include <concepts>
#include <cstddef>


// Average model of a voltage-source inverter, without switching and without dead time. The voltage command of the PMSM is
// limited to the linear range of a space-vector modulator: in the amplitude-invariant dq frame the largest voltage vector that
// the inverter can make has magnitude Vdc / sqrt(3) (Holtz, Pulsewidth modulation for electronic power conversion, Proc. IEEE,
// 1994). A command beyond the limit is scaled onto the limit circle, keeping its direction.
namespace Simulation {


template<std::floating_point T>
class Inverter {
public:
	struct Signals {
	};


	static constexpr std::size_t states = 0;
	static constexpr int stage = 1;


private:
	T dcBus = 0;
	T appliedD = 0;
	T appliedQ = 0;


public:
	constexpr Inverter() = default;


	// The DC bus voltage, V.
	constexpr void SetDcBus(T value) {
		dcBus = value;
	}


	template<typename Bus>
	constexpr void Publish(const T*, Bus& bus) const {
		const T magnitude = std::sqrt(bus.voltageD * bus.voltageD + bus.voltageQ * bus.voltageQ);
		const T limit = dcBus / std::sqrt(static_cast<T>(3));

		if (magnitude > limit) {
			const T scale = limit / magnitude;
			bus.voltageD *= scale;
			bus.voltageQ *= scale;
		}
	}


	template<typename Bus>
	constexpr void Derivative(const T*, const Bus&, T*) const {
	}


	constexpr void Initialize(T*) const {
	}


	template<typename Bus>
	void Latch(const Bus& bus) {
		appliedD = bus.voltageD;
		appliedQ = bus.voltageQ;
	}


	// The voltage applied to the motor, V, in the dq frame, at the end of the last period.
	constexpr T AppliedD() const {
		return appliedD;
	}


	constexpr T AppliedQ() const {
		return appliedQ;
	}
};


}
