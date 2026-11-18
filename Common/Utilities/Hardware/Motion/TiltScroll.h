#pragma once
#include <Utilities/DataTypes.h>
#include <cmath>


// Turns the tilt of a hand-held device into a scroll target for a text view. The input is accelerometer (g) and
// gyroscope (deg/s) samples with timestamps; the output is a target offset in pixels that the caller applies to the
// view (for example with lv_obj_scroll_to_y). The angles come from a complementary filter: the gyroscope is integrated
// and the gravity direction corrects it. Portable: no VHAL adapters, no RTOS and no UI library. See the wiki page for
// the formulas and the sources of the defaults.
class TiltScroll {
public:
	// Hold: the tilt angles are still tracked, but the target changes only through NudgePx (for example by taps).
	enum class Mode : uint8 {
		Position,
		Rate,
		Hold
	};


	enum class Axis : uint8 {
		Pitch,
		Roll
	};


	// Slosh: a positive deviation moves the text up, so later lines come into view. Pointing is the reverse.
	enum class Direction : uint8 {
		Slosh,
		Pointing
	};


	struct Sample {
		uint64 timestampUs = 0;
		float accelX = 0.0f;
		float accelY = 0.0f;
		float accelZ = 0.0f;
		float gyroX = 0.0f;
		float gyroY = 0.0f;
		float gyroZ = 0.0f;
	};


	struct Tuning {
		float pixelsPerDegree = 12.0f;
		float positionRangeDeg = 20.0f;
		float excessPxPerSecPerDeg = 5.0f;
		float deadZoneDeg = 4.0f;
		float rateGainPxPerSec = 2.0f;
		float rateExponent = 1.5f;
		float maxRatePxPerSec = 300.0f;
		float complementaryTauSec = 1.0f;
		float accelLowPassHz = 10.0f;
		// Must be greater than zero. The gravity correction is weighted down by |accel| - 1 g beyond this window.
		float accelTrustG = 0.25f;
		float stillGyroDegPerSec = 5.0f;
		// 0 switches the automatic neutral reset off.
		uint32 stillRezeroMs = 2000;
		// A gap longer than this restarts the angles from gravity instead of integrating across it.
		float maxGapSec = 0.2f;
	};


private:
	static constexpr float radToDeg = 57.29577951f;
	static constexpr float twoPi = 6.28318531f;

	Mode mode = Mode::Position;
	Axis axis = Axis::Pitch;
	Direction direction = Direction::Slosh;
	Tuning tuning;
	float scrollMaxPx = 0.0f;

	bool hasSample = false;
	uint64 lastTimeUs = 0;
	float pitchDeg = 0.0f;
	float rollDeg = 0.0f;
	float accelPitchLowDeg = 0.0f;
	float accelRollLowDeg = 0.0f;
	float zeroDeg = 0.0f;
	float targetPx = 0.0f;
	// Position mode: the target minus the in-range part of the deviation, so changes of the zero do not move the text.
	float anchorPx = 0.0f;
	float stillSec = 0.0f;
	bool stillRezeroed = false;


public:
	void SetTuning(const Tuning& value) {
		tuning = value;
		RebaseAnchor();
	}


	void SetMode(Mode value) {
		mode = value;
		RebaseAnchor();
	}


	void SetAxis(Axis value) {
		axis = value;
		zeroDeg = AxisAngle();
		RebaseAnchor();
	}


	void SetDirection(Direction value) {
		direction = value;
		RebaseAnchor();
	}


	// The largest scroll offset in pixels; the target is clamped to [0, maxPx].
	void SetScrollRange(float maxPx) {
		scrollMaxPx = maxPx < 0.0f ? 0.0f : maxPx;
		targetPx = ClampValue(targetPx, 0.0f, scrollMaxPx);
		RebaseAnchor();
	}


	// Moves the target by deltaPx, clamped to the scroll range. The neutral angle is kept, and the anchor is rebased so
	// that the next tilt update continues from the new target.
	void NudgePx(float deltaPx) {
		targetPx = ClampValue(targetPx + deltaPx, 0.0f, scrollMaxPx);
		RebaseAnchor();
	}


	// Captures the current angle of the chosen axis as neutral. The target does not change.
	void Rezero() {
		if (!hasSample) {
			return;
		}

		zeroDeg = AxisAngle();
		RebaseAnchor();
	}


	void Update(const Sample& sample) {
		float accelPitch = 0.0f;
		float accelRoll = 0.0f;
		AccelAngles(sample, accelPitch, accelRoll);

		if (!hasSample) {
			hasSample = true;
			lastTimeUs = sample.timestampUs;
			Reseed(accelPitch, accelRoll);
			zeroDeg = AxisAngle();
			RebaseAnchor();
			return;
		}

		if (sample.timestampUs <= lastTimeUs) {
			return;
		}

		float dtSec = static_cast<float>(sample.timestampUs - lastTimeUs) * 0.000001f;
		lastTimeUs = sample.timestampUs;
		if (dtSec > tuning.maxGapSec) {
			Reseed(accelPitch, accelRoll);
			RebaseAnchor();
			return;
		}

		FilterAngles(sample, accelPitch, accelRoll, dtSec);
		TrackStillness(sample, dtSec);
		Control(dtSec);
	}


	// Target scroll offset in pixels, in [0, scroll range]. The caller applies it to the view.
	float GetTargetPx() const {
		return targetPx;
	}


	// Filtered angle of the chosen axis in degrees, measured from the gravity direction.
	float GetAngleDeg() const {
		return AxisAngle();
	}


	float GetZeroDeg() const {
		return zeroDeg;
	}


	float GetPitchDeg() const {
		return pitchDeg;
	}


	float GetRollDeg() const {
		return rollDeg;
	}


	Mode GetMode() const {
		return mode;
	}


	Axis GetAxis() const {
		return axis;
	}


	Direction GetDirection() const {
		return direction;
	}


private:
	float AxisAngle() const {
		return axis == Axis::Pitch ? pitchDeg : rollDeg;
	}


	// Signed deviation from the neutral angle, with the direction applied.
	float Deviation() const {
		float raw = AxisAngle() - zeroDeg;
		return direction == Direction::Slosh ? raw : -raw;
	}


	// Keeps the target where it is after the zero, the axis, the direction or the tuning changes.
	void RebaseAnchor() {
		float range = tuning.positionRangeDeg;
		float inRange = ClampValue(Deviation(), -range, range);
		anchorPx = targetPx - inRange * tuning.pixelsPerDegree;
	}


	void Reseed(float accelPitch, float accelRoll) {
		pitchDeg = accelPitch;
		rollDeg = accelRoll;
		accelPitchLowDeg = accelPitch;
		accelRollLowDeg = accelRoll;
	}


	// Pitch is the rotation about the Y axis of the sensor and roll the rotation about X, both right-handed, so the
	// gyroscope rate of the same axis is the derivative of the angle.
	static void AccelAngles(const Sample& sample, float& pitch, float& roll) {
		float yz = std::sqrt(sample.accelY * sample.accelY + sample.accelZ * sample.accelZ);
		pitch = std::atan2(-sample.accelX, yz) * radToDeg;
		roll = std::atan2(sample.accelY, sample.accelZ) * radToDeg;
	}


	void FilterAngles(const Sample& sample, float accelPitch, float accelRoll, float dtSec) {
		float lowAlpha = 1.0f - std::exp(-twoPi * tuning.accelLowPassHz * dtSec);
		accelPitchLowDeg = accelPitchLowDeg + lowAlpha * (accelPitch - accelPitchLowDeg);
		accelRollLowDeg = accelRollLowDeg + lowAlpha * (accelRoll - accelRollLowDeg);

		// While the device accelerates, the accelerometer measures more than gravity, so its angle is not trusted.
		float accelNorm = std::sqrt(sample.accelX * sample.accelX + sample.accelY * sample.accelY
			+ sample.accelZ * sample.accelZ);
		float trust = 1.0f - std::fabs(accelNorm - 1.0f) / tuning.accelTrustG;
		trust = ClampValue(trust, 0.0f, 1.0f);

		float correction = trust * dtSec / (tuning.complementaryTauSec + dtSec);
		pitchDeg = (pitchDeg + sample.gyroY * dtSec) * (1.0f - correction) + accelPitchLowDeg * correction;
		rollDeg = (rollDeg + sample.gyroX * dtSec) * (1.0f - correction) + accelRollLowDeg * correction;
	}


	void TrackStillness(const Sample& sample, float dtSec) {
		if (tuning.stillRezeroMs == 0) {
			return;
		}

		float gyroNorm = std::sqrt(sample.gyroX * sample.gyroX + sample.gyroY * sample.gyroY
			+ sample.gyroZ * sample.gyroZ);
		if (gyroNorm >= tuning.stillGyroDegPerSec) {
			stillSec = 0.0f;
			stillRezeroed = false;
			return;
		}

		stillSec = stillSec + dtSec;
		if (!stillRezeroed && stillSec * 1000.0f >= static_cast<float>(tuning.stillRezeroMs)) {
			stillRezeroed = true;
			Rezero();
		}
	}


	void Control(float dtSec) {
		if (mode == Mode::Hold) {
			return;
		}

		float deviation = Deviation();

		if (mode == Mode::Position) {
			float range = tuning.positionRangeDeg;
			float inRange = ClampValue(deviation, -range, range);
			float excess = deviation - inRange;

			// Beyond the range the excess angle moves the anchor, so the text keeps scrolling while the device is held.
			anchorPx = anchorPx + excess * tuning.excessPxPerSecPerDeg * dtSec;
			targetPx = ClampValue(anchorPx + inRange * tuning.pixelsPerDegree, 0.0f, scrollMaxPx);
			anchorPx = targetPx - inRange * tuning.pixelsPerDegree;
			return;
		}

		float beyondDeadZone = std::fabs(deviation) - tuning.deadZoneDeg;
		float speed = 0.0f;
		if (beyondDeadZone > 0.0f) {
			speed = tuning.rateGainPxPerSec * std::pow(beyondDeadZone, tuning.rateExponent);
			if (deviation < 0.0f) {
				speed = -speed;
			}
			speed = ClampValue(speed, -tuning.maxRatePxPerSec, tuning.maxRatePxPerSec);
		}

		targetPx = ClampValue(targetPx + speed * dtSec, 0.0f, scrollMaxPx);
	}


	static float ClampValue(float value, float low, float high) {
		if (value < low) {
			return low;
		}

		if (value > high) {
			return high;
		}

		return value;
	}
};
