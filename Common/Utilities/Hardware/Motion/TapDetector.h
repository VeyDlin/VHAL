#pragma once
#include <Utilities/DataTypes.h>
#include <cmath>


// One-pole high-pass filter for the three accelerometer axes. It removes gravity and slow hand motion below the corner
// frequency and keeps the onset of a knock, which lasts a few milliseconds and so has its energy far above 20 Hz. The
// first sample after Reset gives zero output, which is the rest state.
class AccelHighPass {
public:
	void SetCornerHz(float value) {
		cornerHz = value;
	}


	void Reset() {
		initialized = false;
	}


	// Feeds one sample of the three axes, dtSec after the previous one.
	void Update(const float input[3], float dtSec) {
		if (!initialized) {
			initialized = true;
			for (uint32 axis = 0; axis < 3; axis++) {
				previousInput[axis] = input[axis];
				output[axis] = 0.0f;
			}
			return;
		}

		float rc = 1.0f / (twoPi * cornerHz);
		float alpha = rc / (rc + dtSec);
		for (uint32 axis = 0; axis < 3; axis++) {
			output[axis] = alpha * (output[axis] + input[axis] - previousInput[axis]);
			previousInput[axis] = input[axis];
		}
	}


	float GetAxis(uint32 axis) const {
		return output[axis];
	}


private:
	static constexpr float twoPi = 6.28318531f;

	float cornerHz = 20.0f;
	bool initialized = false;
	float previousInput[3] = { 0.0f, 0.0f, 0.0f };
	float output[3] = { 0.0f, 0.0f, 0.0f };
};


// Detects knocks on the faces of a device from 1 kHz accelerometer samples. The detector works on the jerk (the time
// derivative) of the high-passed acceleration, so hand motion does not count as a knock. An event starts when the jerk
// norm reaches the candidate level and ends after a short quiet time. It is then classified:
//   - SixFaces: the face is the axis with the largest jerk at the peak of the event, with the sign of that component;
//   - TwoFaces: only the two calibrated faces count. The axis whose first jerk lobe starts earlier decides (ringing comes
//     after the impact, so a larger lobe on the other axis does not turn the knock), and the sign of that lobe must match
//     the calibrated face; anything else is OtherFace.
// Single taps are reported at once unless doubleTapEnabled is set, in which case a second tap on the same face within the
// double window makes a double tap and a single tap waits for that window.
class TapDetector {
public:
	// The order of the faces is the order of the sensor axes, positive face before negative face.
	enum class Face : uint8 {
		PlusX,
		MinusX,
		PlusY,
		MinusY,
		PlusZ,
		MinusZ
	};


	enum class Kind : uint8 {
		Single,
		Double
	};


	enum class Reason : uint8 {
		Refractory,
		Moving,
		Weak,
		GyroNotConfirmed,
		OtherFace
	};


	enum class Classification : uint8 {
		SixFaces,
		TwoFaces
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


	struct Tap {
		Face face = Face::PlusX;
		Kind kind = Kind::Single;
		float peakGPerS = 0.0f;
		uint64 timeUs = 0;
	};


	struct Rejection {
		Face face = Face::PlusX;
		Reason reason = Reason::Weak;
		float peakGPerS = 0.0f;
		uint64 timeUs = 0;
	};


	struct Tuning {
		Classification classification = Classification::SixFaces;
		// The two calibrated faces of Classification::TwoFaces. They must lie on different axes.
		Face twoFaces[2] = { Face::PlusX, Face::PlusZ };
		// Corner of the high-pass filter on the accelerometer. Hand motion is below about 10 Hz, a knock onset above 50 Hz.
		float highPassHz = 20.0f;
		// Jerk that starts an event. Also the level below which an event is considered over.
		float candidateGPerS = 25.0f;
		// Peak jerk an event needs to be a tap, per face (PlusX, MinusX, PlusY, MinusY, PlusZ, MinusZ).
		float faceGPerS[6] = { 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f };
		// Mean jerk magnitude (exponential average) above which the device counts as moving.
		float movingGPerS = 12.0f;
		float activityTauSec = 0.2f;
		// This many candidate events starting within burstWindowSec of each other count as moving. The exponential
		// activity starts from rest, so a shake would otherwise be accepted until the average catches up.
		uint32 burstEvents = 3;
		float burstWindowSec = 0.3f;
		float refractorySec = 0.08f;
		// Off: a single tap is reported at once. On: a single tap waits doubleWindowSec for a second tap on the same face.
		bool doubleTapEnabled = true;
		float doubleWindowSec = 0.35f;
		float eventMaxSec = 0.04f;
		float eventQuietSec = 0.015f;
		// 0 switches the gyroscope confirmation off. Otherwise the gyroscope norm must reach this value during the event.
		float gyroConfirmDps = 0.0f;
		// A gap longer than this between samples restarts the filter and drops the open event.
		float maxGapSec = 0.005f;
	};


private:
	static constexpr uint32 queueSize = 8;

	// The first jerk lobe of one calibrated axis in the current event: a run of samples above the candidate level.
	struct Lobe {
		bool started = false;
		bool done = false;
		bool negative = false;
		uint64 startUs = 0;
		float peakGPerS = 0.0f;
	};

	Tuning tuning;
	AccelHighPass highPass;
	bool hasPrevious = false;
	float previousHigh[3] = { 0.0f, 0.0f, 0.0f };
	uint64 previousTimeUs = 0;
	float activity = 0.0f;

	bool inEvent = false;
	uint64 eventStartUs = 0;
	float eventPeakGPerS = 0.0f;
	Face eventFace = Face::PlusX;
	float eventQuietSec = 0.0f;
	float eventGyroPeakDps = 0.0f;
	float activityAtStart = 0.0f;
	bool hasPreviousEvent = false;
	uint64 previousEventStartUs = 0;
	uint32 burstCount = 0;
	uint32 burstAtStart = 0;
	Lobe lobes[2];

	bool hasAccepted = false;
	uint64 lastAcceptedUs = 0;

	bool hasPending = false;
	Tap pending;

	Tap taps[queueSize];
	uint32 tapHead = 0;
	uint32 tapCount = 0;
	Rejection rejections[queueSize];
	uint32 rejectionHead = 0;
	uint32 rejectionCount = 0;


public:
	void SetTuning(const Tuning& value) {
		tuning = value;
		highPass.SetCornerHz(value.highPassHz);
	}


	// Returns the oldest accepted tap that has not been popped.
	bool PopTap(Tap& out) {
		if (tapCount == 0) {
			return false;
		}

		out = taps[tapHead];
		tapHead = (tapHead + 1) % queueSize;
		tapCount--;
		return true;
	}


	bool PopRejection(Rejection& out) {
		if (rejectionCount == 0) {
			return false;
		}

		out = rejections[rejectionHead];
		rejectionHead = (rejectionHead + 1) % queueSize;
		rejectionCount--;
		return true;
	}


	// The mean jerk magnitude in g/s of the high-passed acceleration. The quiet check compares it with Tuning::movingGPerS.
	float GetActivityGPerS() const {
		return activity;
	}


	static Face Opposite(Face face) {
		return static_cast<Face>(static_cast<uint8>(face) ^ 1);
	}


	void Update(const Sample& sample) {
		float accel[3] = { sample.accelX, sample.accelY, sample.accelZ };
		float gyroNorm = std::sqrt(sample.gyroX * sample.gyroX + sample.gyroY * sample.gyroY
			+ sample.gyroZ * sample.gyroZ);

		if (!hasPrevious) {
			StartFilter(accel, sample.timestampUs);
			return;
		}

		if (sample.timestampUs <= previousTimeUs) {
			return;
		}

		float dtSec = static_cast<float>(sample.timestampUs - previousTimeUs) * 0.000001f;
		if (dtSec > tuning.maxGapSec) {
			inEvent = false;
			StartFilter(accel, sample.timestampUs);
			return;
		}

		highPass.Update(accel, dtSec);
		float jerk[3] = { 0.0f, 0.0f, 0.0f };
		for (uint32 axis = 0; axis < 3; axis++) {
			float high = highPass.GetAxis(axis);
			jerk[axis] = (high - previousHigh[axis]) / dtSec;
			previousHigh[axis] = high;
		}
		previousTimeUs = sample.timestampUs;

		float jerkMag = std::sqrt(jerk[0] * jerk[0] + jerk[1] * jerk[1] + jerk[2] * jerk[2]);
		uint32 peakAxis = 0;
		for (uint32 axis = 1; axis < 3; axis++) {
			if (std::fabs(jerk[axis]) > std::fabs(jerk[peakAxis])) {
				peakAxis = axis;
			}
		}
		Face face = FaceOf(peakAxis, jerk[peakAxis] < 0.0f);

		CheckPendingTimeout(sample.timestampUs);

		if (!inEvent && jerkMag >= tuning.candidateGPerS) {
			inEvent = true;
			eventStartUs = sample.timestampUs;
			eventPeakGPerS = 0.0f;
			eventFace = face;
			eventQuietSec = 0.0f;
			eventGyroPeakDps = 0.0f;
			activityAtStart = activity;
			lobes[0] = Lobe();
			lobes[1] = Lobe();

			if (hasPreviousEvent && Seconds(previousEventStartUs, eventStartUs) < tuning.burstWindowSec) {
				burstCount++;
			} else {
				burstCount = 1;
			}
			burstAtStart = burstCount;
			previousEventStartUs = eventStartUs;
			hasPreviousEvent = true;
		}

		if (inEvent) {
			if (jerkMag > eventPeakGPerS) {
				eventPeakGPerS = jerkMag;
				eventFace = face;
			}
			if (gyroNorm > eventGyroPeakDps) {
				eventGyroPeakDps = gyroNorm;
			}
			if (tuning.classification == Classification::TwoFaces) {
				TrackLobes(jerk, sample.timestampUs);
			}

			if (jerkMag < tuning.candidateGPerS) {
				eventQuietSec = eventQuietSec + dtSec;
			} else {
				eventQuietSec = 0.0f;
			}

			float elapsedSec = Seconds(eventStartUs, sample.timestampUs);
			if (eventQuietSec >= tuning.eventQuietSec || elapsedSec >= tuning.eventMaxSec) {
				inEvent = false;
				FinishEvent();
			}
		}

		float weight = dtSec / tuning.activityTauSec;
		weight = weight > 1.0f ? 1.0f : weight;
		activity = activity + (jerkMag - activity) * weight;
	}


private:
	void StartFilter(const float accel[3], uint64 timeUs) {
		highPass.Reset();
		highPass.Update(accel, 0.0f);
		for (uint32 axis = 0; axis < 3; axis++) {
			previousHigh[axis] = 0.0f;
		}
		previousTimeUs = timeUs;
		hasPrevious = true;
	}


	static uint32 AxisOf(Face face) {
		return static_cast<uint32>(face) / 2;
	}


	static Face FaceOf(uint32 axis, bool negative) {
		return static_cast<Face>(axis * 2 + (negative ? 1 : 0));
	}


	float Seconds(uint64 fromUs, uint64 toUs) const {
		return static_cast<float>(toUs - fromUs) * 0.000001f;
	}


	void TrackLobes(const float jerk[3], uint64 timeUs) {
		for (uint32 index = 0; index < 2; index++) {
			Lobe& lobe = lobes[index];
			if (lobe.done) {
				continue;
			}

			float value = jerk[AxisOf(tuning.twoFaces[index])];
			float magnitude = std::fabs(value);
			if (magnitude >= tuning.candidateGPerS) {
				if (!lobe.started) {
					lobe.started = true;
					lobe.startUs = timeUs;
					lobe.negative = value < 0.0f;
				}
				lobe.peakGPerS = magnitude > lobe.peakGPerS ? magnitude : lobe.peakGPerS;
			} else if (lobe.started) {
				lobe.done = true;
			}
		}
	}


	// The calibrated axis whose first lobe starts first; equal starts go to the larger peak. Returns -1 when neither started.
	int32 WinningLobe() const {
		int32 winner = -1;
		for (uint32 index = 0; index < 2; index++) {
			const Lobe& lobe = lobes[index];
			if (!lobe.started) {
				continue;
			}

			if (winner < 0) {
				winner = static_cast<int32>(index);
				continue;
			}

			const Lobe& current = lobes[winner];
			bool earlier = lobe.startUs < current.startUs;
			bool sameStartLarger = lobe.startUs == current.startUs && lobe.peakGPerS > current.peakGPerS;
			if (earlier || sameStartLarger) {
				winner = static_cast<int32>(index);
			}
		}
		return winner;
	}


	void FinishEvent() {
		if (hasAccepted && Seconds(lastAcceptedUs, eventStartUs) < tuning.refractorySec) {
			PushRejection(eventFace, Reason::Refractory, eventPeakGPerS, eventStartUs);
			return;
		}

		if (activityAtStart >= tuning.movingGPerS || burstAtStart >= tuning.burstEvents) {
			PushRejection(eventFace, Reason::Moving, eventPeakGPerS, eventStartUs);
			return;
		}

		Face face = eventFace;
		float peakGPerS = eventPeakGPerS;
		if (tuning.classification == Classification::TwoFaces) {
			int32 winner = WinningLobe();
			if (winner < 0) {
				PushRejection(eventFace, Reason::OtherFace, eventPeakGPerS, eventStartUs);
				return;
			}

			const Lobe& lobe = lobes[winner];
			Face calibrated = tuning.twoFaces[winner];
			Face observed = FaceOf(AxisOf(calibrated), lobe.negative);
			if (observed != calibrated) {
				PushRejection(observed, Reason::OtherFace, lobe.peakGPerS, eventStartUs);
				return;
			}
			face = calibrated;
			peakGPerS = lobe.peakGPerS;
		}

		uint32 faceIndex = static_cast<uint32>(face);
		if (peakGPerS < tuning.faceGPerS[faceIndex]) {
			PushRejection(face, Reason::Weak, peakGPerS, eventStartUs);
			return;
		}

		if (tuning.gyroConfirmDps > 0.0f && eventGyroPeakDps < tuning.gyroConfirmDps) {
			PushRejection(face, Reason::GyroNotConfirmed, peakGPerS, eventStartUs);
			return;
		}

		hasAccepted = true;
		lastAcceptedUs = eventStartUs;
		Accept(face, peakGPerS, eventStartUs);
	}


	void Accept(Face face, float peakGPerS, uint64 timeUs) {
		if (!tuning.doubleTapEnabled) {
			Tap tap;
			tap.face = face;
			tap.kind = Kind::Single;
			tap.peakGPerS = peakGPerS;
			tap.timeUs = timeUs;
			PushTap(tap);
			return;
		}

		if (hasPending && pending.face == face && Seconds(pending.timeUs, timeUs) <= tuning.doubleWindowSec) {
			Tap tap;
			tap.face = face;
			tap.kind = Kind::Double;
			tap.peakGPerS = pending.peakGPerS > peakGPerS ? pending.peakGPerS : peakGPerS;
			tap.timeUs = timeUs;
			hasPending = false;
			PushTap(tap);
			return;
		}

		if (hasPending) {
			PushTap(pending);
		}

		pending.face = face;
		pending.kind = Kind::Single;
		pending.peakGPerS = peakGPerS;
		pending.timeUs = timeUs;
		hasPending = true;
	}


	void CheckPendingTimeout(uint64 nowUs) {
		if (hasPending && Seconds(pending.timeUs, nowUs) > tuning.doubleWindowSec) {
			PushTap(pending);
			hasPending = false;
		}
	}


	void PushTap(const Tap& tap) {
		uint32 slot = (tapHead + tapCount) % queueSize;
		if (tapCount == queueSize) {
			slot = tapHead;
			tapHead = (tapHead + 1) % queueSize;
		} else {
			tapCount++;
		}
		taps[slot] = tap;
	}


	void PushRejection(Face face, Reason reason, float peakGPerS, uint64 timeUs) {
		Rejection rejection;
		rejection.face = face;
		rejection.reason = reason;
		rejection.peakGPerS = peakGPerS;
		rejection.timeUs = timeUs;

		uint32 slot = (rejectionHead + rejectionCount) % queueSize;
		if (rejectionCount == queueSize) {
			slot = rejectionHead;
			rejectionHead = (rejectionHead + 1) % queueSize;
		} else {
			rejectionCount++;
		}
		rejections[slot] = rejection;
	}
};
