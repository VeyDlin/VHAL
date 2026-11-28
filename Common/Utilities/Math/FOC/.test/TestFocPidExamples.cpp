#include <Utilities/Math/FOC/ClarkeTransformation.h>
#include <Utilities/Math/FOC/ParkTransformation.h>
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>
#include <Utilities/Math/FOC/SlidingModeObserver.h>
#include <Utilities/Math/FOC/VoltageCalculator.h>
#include <Utilities/Math/FOC/SpeedCalculator.h>
#include <Utilities/Math/PID/PidController.h>
#include <VhalCheck.h>
#include <cmath>


// The strict C++23 mode does not define M_PI, which the examples use.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// The PID examples of the FOC wiki page, with their logic unchanged, and run once. The examples call board helpers that the page does not
// define. They are defined here as stand-ins that return zero, so the examples link and run: the ISR functions run once each, with
// zero currents and zero angles. Each example sits in its own namespace because the examples reuse names (encoderOffset, Vdc, ...).


// The duty cycles that the examples write through TIM_SetDuty*, kept for the checks in main.
float timDutyA = 0.0f;
float timDutyB = 0.0f;
float timDutyC = 0.0f;


float AdcToAmps(uint16 raw) {
	(void)raw;
	return 0.0f;
}


uint16 ADC_ReadPhaseA() {
	return 0;
}


uint16 ADC_ReadPhaseB() {
	return 0;
}


uint16 ADC_ReadVbus() {
	return 0;
}


uint32 TIM_GetEncoderCount() {
	return 0;
}


float GetElectricalAngle(uint32 count, uint32 cpr, uint32 polePairs, float offset) {
	(void)count;
	(void)cpr;
	(void)polePairs;
	(void)offset;
	return 0.0f;
}


float GetVdc(uint16 raw) {
	(void)raw;
	return 24.0f;
}


float ReadAbsoluteEncoder() {
	return 0.0f;
}


void TIM_SetDutyA(float duty) {
	timDutyA = duty;
}


void TIM_SetDutyB(float duty) {
	timDutyB = duty;
}


void TIM_SetDutyC(float duty) {
	timDutyC = duty;
}


// Each duty cycle of the last ISR call lies in [0, 1].
void CheckDutyRange() {
	VHAL_CHECK(timDutyA >= 0.0f && timDutyA <= 1.0f);
	VHAL_CHECK(timDutyB >= 0.0f && timDutyB <= 1.0f);
	VHAL_CHECK(timDutyC >= 0.0f && timDutyC <= 1.0f);
}


namespace CurrentLoopExample {
	const uint32 encoderCPR = 4096;
	float encoderOffset = 0.0f;

	// ===== Motor parameters =====
	const float Vdc = 24.0f;          // DC bus voltage [V] (or measure via ADC)
	const uint32 POLE_PAIRS = 4;
	const uint32 PWM_FREQ = 20000;    // 20 kHz

	// ===== Global objects (persist between ISR calls) =====
	ClarkeTransformation<float> clarke;
	ParkTransformation<float> park;
	ParkTransformation<float> invPark;
	SpaceVectorGenerator<float> svg;

	// PI controller for d-axis current: keeps Id = 0 (for SPM motors)
	PidController<float> pidId({
		.proportional = 5.0f,    // Kp — tune: start with Ls * bandwidth
		.integral = 500.0f,      // Ki — tune: start with Rs * bandwidth
		.derivative = 0.0f       // D not used in current loop
	}, {
		.max = Vdc / 2.0f,       // Output limit [V] — no more than half-bus
		.min = -Vdc / 2.0f
	});

	// PI controller for q-axis current: controls torque
	PidController<float> pidIq({
		.proportional = 5.0f,
		.integral = 500.0f,
		.derivative = 0.0f
	}, {
		.max = Vdc / 2.0f,
		.min = -Vdc / 2.0f
	});

	// PI speed controller (outer loop, can be called less frequently)
	PidController<float> pidSpeed({
		.proportional = 0.5f,
		.integral = 10.0f,
		.derivative = 0.0f
	}, {
		.max = 3.5f,              // Limit = rated current [A]
		.min = -3.5f
	});

	float IqRef = 0;  // q-axis current reference [A] — from speed loop

	// ===== ADC interrupt — called at PWM frequency =====
	//
	// This is the PWM timer interrupt (TIM1 on STM32) that drives the power switches.
	// NOT an arbitrary timer — specifically the one that generates PWM for phases A, B, C.
	//
	// Currents Ia, Ib, Ic — from ADC through shunt resistors:
	//   Ia = (ADC_raw - 2048) * (Vref / 4096) / (R_shunt * amplifier_gain) [A]
	//
	// electricalAngle — from encoder:
	//   elecAngle = (encoderCount / CPR) * 2π * polePairs [rad]
	//
	void FOC_ISR() {
		// --- Get input data ---
		// Phase currents: ADC → amperes (see AdcToAmps above)
		float Ia = AdcToAmps(ADC_ReadPhaseA());
		float Ib = AdcToAmps(ADC_ReadPhaseB());
		float Ic = -(Ia + Ib);  // If only 2 shunts

		// Electrical angle: encoder → radians
		float electricalAngle = GetElectricalAngle(
			TIM_GetEncoderCount(), encoderCPR, POLE_PAIRS, encoderOffset
		);

		// --- 1. Measurement: abc → αβ → dq ---
		clarke.SetPhase({Ia, Ib, Ic});
		clarke.Resolve(ClarkeTransformation<float>::CurrentsMode::C3);
		ClarkeTransformation<float>::Clarke ab = clarke.GetClarke();

		park.SetAxis({ab.alpha, ab.beta, electricalAngle});
		park.Resolve();
		ParkTransformation<float>::Park dq = park.GetPark();
		// dq.dAxis — magnetizing current (want = 0 for SPM)
		// dq.qAxis — torque current (controlled via speed PI)

		// --- 2. Current PI controllers ---
		// IdRef = 0 for surface permanent magnets (SPM). For IPM can be < 0 (field weakening)
		// IqRef is set from the outer speed/position loop (or directly for torque control)
		//
		// reference = current setpoint [A]
		// feedback  = measured current [A] (from Park)
		// frequency = Resolve call frequency [Hz] (= PWM frequency)
		float Vd = pidId.SetInpit({
			.feedback  = dq.dAxis,
			.reference = 0.0f,       // Id = 0 for SPM
			.frequency = PWM_FREQ
		}).Resolve().Get();           // Output [V]

		float Vq = pidIq.SetInpit({
			.feedback  = dq.qAxis,
			.reference = IqRef,      // From speed loop [A]
			.frequency = PWM_FREQ
		}).Resolve().Get();           // Output [V]

		// --- 3. Actuation: dq → αβ → PWM duty cycles ---
		invPark.SetPark({Vd, Vq, electricalAngle});
		invPark.ResolveInverse();
		ParkTransformation<float>::Axis vab = invPark.GetAxis();
		// vab.alpha, vab.beta — voltages in stationary frame [V]

		// Normalize to half-bus: SVGEN expects input in approximately [-1, 1] range
		svg.Set(vab.alpha / (Vdc / 2.0f), vab.beta / (Vdc / 2.0f));
		svg.ResolveCommonMode();
		SpaceVectorGenerator<float>::Out pwm = svg.Get();
		// pwm.phaseA/B/C — in range [-1, 1]

		// Set PWM duty cycle: convert [-1, 1] → [0, 1]
		// 0.0 = low-side switch always on, 1.0 = high-side switch always on
		// These are values for PWM timer compare register:
		//   TIM1->CCR1 = duty * TIM1->ARR
		TIM_SetDutyA((pwm.phaseA + 1.0f) * 0.5f);
		TIM_SetDutyB((pwm.phaseB + 1.0f) * 0.5f);
		TIM_SetDutyC((pwm.phaseC + 1.0f) * 0.5f);
	}

	// ===== Speed loop (can be called less frequently — e.g., every 10th FOC call) =====
	void SpeedLoop_Update(float targetSpeedRPM, float measuredSpeedRPM) {
		IqRef = pidSpeed.SetInpit({
			.feedback  = measuredSpeedRPM,
			.reference = targetSpeedRPM,
			.frequency = PWM_FREQ / 10   // If called every 10th time
		}).Resolve().Get();              // Output = IqRef [A]
	}
}


namespace SensorlessExample {
	// ===== Motor parameters (from datasheet or measurements) =====
	const float Rs = 0.4f;       // Stator resistance [Ohm] (measure with multimeter between phases / 2)
	const float Ls = 0.6e-3f;    // Stator inductance [H] (measure with LCR meter between phases / 2)
	const float Irated = 3.5f;   // Rated current [A] (from datasheet)
	const float Vdc = 24.0f;     // DC bus voltage [V]
	const uint32 PWM_FREQ = 20000;
	const float Ts = 1.0f / PWM_FREQ;

	// Base values for normalization (per-unit system)
	const float Ibase = Irated;
	const float Vbase = Vdc / sqrtf(3.0f);   // Line-to-line → phase

	// ===== Objects =====
	SlidingModeObserver<float> smo;
	SpeedCalculator<float> speedCalc;
	ClarkeTransformation<float> clarke;
	ParkTransformation<float> park, invPark;
	VoltageCalculator<float> voltCalc;
	SpaceVectorGenerator<float> svg;

	// Current PI controllers (same as FOC for PMSM example)
	PidController<float> pidId({.proportional = 5.0f, .integral = 500.0f}, {.max = Vdc/2, .min = -Vdc/2});
	PidController<float> pidIq({.proportional = 5.0f, .integral = 500.0f}, {.max = Vdc/2, .min = -Vdc/2});

	float IqRef = 0;  // From speed loop [A]

	// Previous step duty cycles — needed by VoltageCalculator to reconstruct voltages
	float lastDutyA = 0.5f, lastDutyB = 0.5f, lastDutyC = 0.5f;

	void SensorlessFOC_Init() {
		// SMO: constants computed from motor parameters
		smo.constants = SlidingModeObserver<float>::ComputeConstants({
			.Rs = Rs,
			.Ls = Ls,
			.Ib = Ibase,
			.Vb = Vbase,
			.Ts = Ts
		});
		smo.Kslide = 0.1f;    // Start with 0.05-0.2, increase if angle is noisy
		smo.Kslf   = 0.05f;   // Back-EMF filter: smaller = stronger filtering, more delay

		// SpeedCalculator
		// K1 = differentiator coefficient: 1 / (baseFreq * Ts)
		//   baseFreq = (ratedRPM / 60) * polePairs
		//   Example: 3600 RPM, 4pp → baseFreq = 240 Hz → K1 = 1/(240*50e-6) = 83.3
		speedCalc.K1 = 83.3f;
		speedCalc.K2 = 0.95f;  // LPF: higher = stronger smoothing (0.9-0.99)
		speedCalc.K3 = 0.05f;  // K2 + K3 = 1.0
		speedCalc.baseSpeedRpm = 3600;
	}

	// ===== ADC interrupt (triggered by motor PWM timer) =====
	//
	// Same PWM timer that drives the power switches.
	// No arguments passed — everything is read from ADC and previous duty cycles inside ISR.
	//
	void SensorlessFOC_ISR() {
		// --- Input data ---

		// Phase currents: ADC → amperes
		float Ia = AdcToAmps(ADC_ReadPhaseA());
		float Ib = AdcToAmps(ADC_ReadPhaseB());
		float Ic = -(Ia + Ib);

		// DC bus voltage (can measure each time or use a constant)
		float vdc = GetVdc(ADC_ReadVbus());

		// --- 1. Reconstruct αβ voltages from previous step duty cycles ---
		// VoltageCalculator computes phase voltages from modulation
		voltCalc.Set(vdc, lastDutyA, lastDutyB, lastDutyC);
		voltCalc.Resolve();
		VoltageCalculator<float>::Out volt = voltCalc.Get();

		// --- 2. Clarke: phase currents → αβ ---
		clarke.SetPhase({Ia, Ib, Ic});
		clarke.Resolve(ClarkeTransformation<float>::CurrentsMode::C3);
		ClarkeTransformation<float>::Clarke ab = clarke.GetClarke();

		// --- 3. SMO: rotor angle estimation from back-EMF ---
		// Inputs are normalized to base values (per-unit)
		smo.Set(
			volt.alpha / Vbase,    // αβ voltage [pu]
			volt.beta  / Vbase,
			ab.alpha   / Ibase,    // αβ current [pu]
			ab.beta    / Ibase
		).Resolve();

		float thetaNorm = smo.Get().theta;                  // Angle [0..1]
		float estAngle  = thetaNorm * 2.0f * M_PI;          // Angle [rad] — for Park

		// --- 4. Speed estimation ---
		speedCalc.Set(thetaNorm).Resolve();
		// speedCalc.Get().speedRpm — estimated speed [RPM]

		// --- 5. Park: αβ currents → dq (with estimated angle) ---
		park.SetAxis({ab.alpha, ab.beta, estAngle});
		park.Resolve();
		ParkTransformation<float>::Park dq = park.GetPark();

		// --- 6. Current PI controllers ---
		float Vd = pidId.SetInpit({
			.feedback = dq.dAxis, .reference = 0.0f, .frequency = PWM_FREQ
		}).Resolve().Get();

		float Vq = pidIq.SetInpit({
			.feedback = dq.qAxis, .reference = IqRef, .frequency = PWM_FREQ
		}).Resolve().Get();

		// --- 7. Inverse Park + SVGEN → duty cycles ---
		invPark.SetPark({Vd, Vq, estAngle});
		invPark.ResolveInverse();
		ParkTransformation<float>::Axis vab = invPark.GetAxis();

		svg.Set(vab.alpha / (vdc / 2.0f), vab.beta / (vdc / 2.0f));
		svg.ResolveCommonMode();
		SpaceVectorGenerator<float>::Out pwm = svg.Get();

		// Set PWM duty cycle and save for next step
		lastDutyA = (pwm.phaseA + 1.0f) * 0.5f;
		lastDutyB = (pwm.phaseB + 1.0f) * 0.5f;
		lastDutyC = (pwm.phaseC + 1.0f) * 0.5f;

		TIM_SetDutyA(lastDutyA);
		TIM_SetDutyB(lastDutyB);
		TIM_SetDutyC(lastDutyC);
	}
}


namespace VoltageModeExample {
	// ===== Parameters =====
	const uint32 POLE_PAIRS = 16;    // Pole pairs
	const float Vdc = 24.0f;         // DC bus voltage [V]
	const uint32 ISR_FREQ = 5000;    // 5 kHz (for positioning 1-5 kHz is sufficient)

	ParkTransformation<float> invPark;
	SpaceVectorGenerator<float> svg;

	// Position PID — output = voltage amplitude [V]
	// Roll enabled: correct handling of 359° → 1° transition
	PidController<float> positionPID;

	float encoderOffset = 0;         // From calibration (see CalibrateEncoder above)
	float targetAngleDeg = 0;        // Target angle [degrees]

	void VoltageFOC_Init() {
		positionPID
			.SetCoefficients({
				.proportional = 5.0f,
				.integral = 0.5f,
				.derivative = 0.01f
			})
			.SetOuput({
				.max = 5.0f,             // Maximum amplitude [V] (indirectly limits current)
				.min = -5.0f
			})
			.SetRoll({
				.enable = true,
				.maxInput = 360.0f,      // Cyclic data 0..360°
				.minInput = 0.0f
			});
	}

	// ===== PWM timer interrupt (same timer that drives the switches) =====
	// Frequency: 1-5 kHz is sufficient, since position loop bandwidth
	// is typically 10-100 Hz (limited by mechanical inertia).
	void VoltageFOC_ISR() {
		// --- Electrical angle ---
		// Example: absolute magnetic encoder (AS5048A) returns 0..360°
		float mechAngleDeg = ReadAbsoluteEncoder();  // 0..360 [degrees]
		float mechAngleRad = (mechAngleDeg - encoderOffset) * (M_PI / 180.0f);
		float elecAngle = mechAngleRad * POLE_PAIRS;
		// elecAngle [rad] — electrical angle for Park

		// --- Position PID → voltage amplitude ---
		// Roll automatically handles the 0°/360° crossover
		float amplitude = positionPID.SetInpit({
			.feedback  = mechAngleDeg,       // Current angle [degrees]
			.reference = targetAngleDeg,     // Target angle [degrees]
			.frequency = ISR_FREQ
		}).Resolve().Get();
		// amplitude [V] — sign determines direction: + = forward, - = reverse

		// --- Inverse Park: all amplitude on q-axis ---
		// Vd=0 (no unnecessary flux), Vq=amplitude (torque)
		// No +90° shift needed — Park does this automatically:
		//   when Vd=0 and Vq=amplitude, voltage is perpendicular to rotor flux
		invPark.SetPark({0.0f, amplitude, elecAngle});
		invPark.ResolveInverse();
		ParkTransformation<float>::Axis ab = invPark.GetAxis();

		// --- SVPWM → duty cycles ---
		svg.Set(ab.alpha / (Vdc / 2.0f), ab.beta / (Vdc / 2.0f));
		svg.ResolveCommonMode();
		SpaceVectorGenerator<float>::Out pwm = svg.Get();

		// Duty cycle: [-1,1] → [0,1]
		TIM_SetDutyA((pwm.phaseA + 1.0f) * 0.5f);
		TIM_SetDutyB((pwm.phaseB + 1.0f) * 0.5f);
		TIM_SetDutyC((pwm.phaseC + 1.0f) * 0.5f);
	}

	// Set target angle from main loop (main loop, UART, CAN, ...):
	void SetTargetAngle(float degrees) {
		targetAngleDeg = degrees;  // 0..360
	}
}


int main() {
	// The examples are run once: the current loop and the voltage loop with their setup, and the sensorless loop with its setup. Each
	// ISR gets zero inputs from the stand-ins above.
	CurrentLoopExample::SpeedLoop_Update(1000.0f, 0.0f);
	// The speed PI output saturates at its limit, the rated current.
	VHAL_CHECK_NEAR(CurrentLoopExample::IqRef, 3.5f, 1e-6);
	CurrentLoopExample::FOC_ISR();
	CheckDutyRange();
	SensorlessExample::SensorlessFOC_Init();
	SensorlessExample::SensorlessFOC_ISR();
	CheckDutyRange();
	VoltageModeExample::VoltageFOC_Init();
	VoltageModeExample::SetTargetAngle(90.0f);
	VoltageModeExample::VoltageFOC_ISR();
	CheckDutyRange();
	return VhalCheck::Finish("TestFocPidExamples");
}
