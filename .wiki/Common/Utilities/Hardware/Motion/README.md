# Motion

Two portable, header-only utilities for a hand-held device with an accelerometer and a gyroscope. Neither uses VHAL
adapters, an RTOS or a UI library, so the host tests compile them as they are.

- `TiltScroll.h`: tilt of the device to a scroll target (Position, Rate and Hold modes).
- `TapDetector.h`: knocks on the device to taps: one on a calibrated edge, one on a calibrated back face (or six faces).

Both take samples with a microsecond timestamp. The MPU6886 driver (`Drivers/Sensor/MPU6886`) produces them at 1 kHz from
its FIFO on the M5StickC PLUS2 (see its page).

Host tests: `tools/vhal-host-tests/motion/run.sh` (synthetic traces for both utilities, and the driver against a simulated
device).

## Sources

Designs and defaults come from the literature below. The numbers in the code are starting points, not measured results,
except where a section says it was measured on the board.

- Teather & MacKenzie, "Position vs. Velocity Control for Tilt-Based Interaction", Graphics Interface 2014: position
  control was about twice as fast as rate control (1555 ms vs 3095 ms; 3.3 vs 1.2 bits/s).
- Oakley & O'Modhrain, "Tilt to Scroll": position control again, the same finding.
- US 6,624,824: neutral orientation is captured when scrolling starts.
- Hinckley et al., 2000: a clutch button for the neutral angle. The clutch tires users when reading long text, so the
  auto-rezero below replaces it.
- Bartlett, "Rock 'n' Scroll", WRL 2000/3: scrolling on by default, the neutral reset by holding still for about two
  seconds, a low-pass filter of about 10 Hz plus exponential averaging.
- Users split on the direction of tilt (the "sloshers" 7 of 15, the "pointers" 5 of 15 in the same literature), so the
  direction is a setting.
- Hudson, Harrison, Harrison, LaMarca, "Whack Gestures: Inexpensive and Expressive Sensing of Striking Gestures", TEI 2010:
  strikes on the device can be detected with high accuracy and few false positives by subtracting an exponentially
  decaying average from the raw acceleration.
- US patent 8,442,797, "Directional tap detection algorithm using an accelerometer": classify a tap by the axis whose
  acceleration derivative (jerk) has the largest magnitude; its sign gives the struck face; per-direction thresholds.
  The test firmware does not use the largest-jerk rule for its gestures any more (see TapDetector, two faces).
- ADXL344 datasheet: tapping a face gives a negative spike on that face's positive axis. The sign convention depends on how
  the sensor is mounted, so it is determined on the board (see the test firmware).
- Back-of-device taps as an input gesture: iOS Back Tap (Accessibility) and Android's Quick Tap feature trigger actions on
  taps on the back of the phone. The test firmware uses the same idea: a knock on the back of the case is a second gesture
  next to the knock on the top edge.
- Wear OS removed flick-to-scroll; snapping gestures strain the wrist. No flick is implemented.

## TiltScroll

Input: `Sample` with `timestampUs`, accelerometer `accelX/Y/Z` (g) and gyroscope `gyroX/Y/Z` (deg/s, bias already removed
by the caller). Output: `GetTargetPx()`, the scroll offset in pixels, clamped to `[0, scroll range]`.

### Angles

Pitch is the rotation about the sensor's Y axis, roll the rotation about X. Both are right-handed, so the gyroscope rate of
an axis is the derivative of its angle:

- accelerometer angles: `pitch = atan2(-ax, sqrt(ay^2 + az^2))`, `roll = atan2(ay, az)`
- the accelerometer angle is low-passed at `accelLowPassHz` = 10 Hz: `alpha = 1 - exp(-2 pi f dt)`
- complementary filter with time constant `complementaryTauSec` = 1 s:
  `correction = trust * dt / (tau + dt)`, `angle = (angle + rate * dt) * (1 - correction) + accelLowPass * correction`
- `trust = clamp(1 - | |a| - 1 g | / accelTrustG, 0, 1)` with `accelTrustG` = 0.25 g. While the device accelerates, the
  accelerometer measures more than gravity and its angle is not used.

Consequence: a constant gyroscope bias `b` settles at an angle error of about `b * tau`. A bias of 0.5 deg/s gives 0.5 deg
(6 px at the default gain), not a drift of 5 deg over 10 s (a host test checks this).

A gap longer than `maxGapSec` (0.2 s) restarts the angles from gravity instead of integrating across it.

### Neutral (zero) and the anchor

`Rezero()` captures the current angle of the chosen axis as neutral (`zero`). The first sample sets it. The deviation is

`d = sign * (angle - zero)`, with `sign` = +1 for Slosh and -1 for Pointing.

Slosh: a positive deviation moves the text up (later lines come into view). Pointing is the reverse.

The brief's mapping `target = (angle - zero) * pixelsPerDegree` would move the text to the top on every `Rezero()`. The
implementation keeps the target where it is instead: the anchor `anchorPx` is the target minus the in-range part of the
deviation. Rezero, `SetAxis`, `SetDirection`, `SetMode` and `NudgePx` rebase the anchor, so the target does not jump.

### Position mode

`dc = clamp(d, -positionRangeDeg, +positionRangeDeg)`, `excess = d - dc`

- `anchorPx += excess * excessPxPerSecPerDeg * dt` (default 5 px/s per deg beyond the range)
- `target = clamp(anchorPx + dc * pixelsPerDegree, 0, scrollMax)` (default 12 px per deg, range 20 deg)
- `anchorPx = target - dc * pixelsPerDegree` (anti-windup: the anchor is what the clamped target implies)

So 20 deg of tilt covers 240 px, and a longer text keeps moving slowly beyond that.

### Rate mode

`beyond = |d| - deadZoneDeg` (default 4 deg). If `beyond > 0`:
`speed = sign(d) * min(rateGainPxPerSec * beyond^rateExponent, maxRatePxPerSec)`, with `rateGainPxPerSec` = 2,
`rateExponent` = 1.5 and `maxRatePxPerSec` = 300. Then `target = clamp(target + speed * dt, 0, scrollMax)`.
At 6 deg beyond the dead zone the speed is `2 * 6^1.5` = 29.4 px/s.

### Hold mode

The angles are tracked, the target does not change through the tilt. Only `NudgePx` moves it. The test firmware uses this
mode for the knock gestures (taps only).

### Auto-rezero

When the gyroscope norm stays below `stillGyroDegPerSec` (5 deg/s) for `stillRezeroMs` (2000 ms), `Rezero()` runs once. The
next rezero needs the gyroscope to exceed the threshold again. `stillRezeroMs = 0` switches it off. The threshold is a
guess; hand tremor can exceed it, and the board's rest log is the only measurement so far.

### NudgePx

`NudgePx(delta)` moves the target by `delta` px, clamped to the range, and keeps the neutral angle. The taps use it for
steps and pages. The test firmware spreads a step over 150 ms so it is an animation.

## TapDetector

Input: `Sample` with `timestampUs`, accelerometer (g) and gyroscope (deg/s). Use consecutive samples at a fixed rate; a tap
is 5 to 20 ms, so 100 Hz misses most of them. The test firmware uses the MPU6886 FIFO at 1 kHz.

### High-pass acceleration and jerk

Hand motion is below about 10 Hz; a knock's onset has energy above about 50 Hz. The detector therefore works on a one-pole
high-pass filter of each accelerometer axis (`AccelHighPass`, corner `highPassHz` = 20 Hz):

`alpha = RC / (RC + dt)`, `RC = 1 / (2 pi fc)`, `h = alpha * (h_previous + a - a_previous)`

Gravity and slow motion leave, the onset of a knock stays. Then the jerk of the filtered signal, `J = (h - h_previous) / dt`,
is what the rest of the detector uses. The filter output is zero at the first sample after a reset (a gap longer than
`maxGapSec`, or the start), which is the rest state.

Why 20 Hz (the test model; there is no measured spectrum of a knock in the logs): with a 0.3 g sway at 8 Hz, the filter keeps
`|H| = 8 / sqrt(8^2 + 20^2) = 0.37`, so the residual is about 0.11 g and its jerk about 5.6 g/s (mean magnitude 3.6 g/s). At
2 Hz it keeps 0.10 of the sway. At 50 Hz it keeps `50 / sqrt(50^2 + 20^2) = 0.93` of a knock's onset, so the peak jerk of a
knock is almost the same as without the filter. 25 Hz would also work (0.30 at 8 Hz, 0.86 at 50 Hz); 20 Hz was kept to
preserve more of the onset.

### Event and classification

An event starts when the jerk norm reaches `candidateGPerS` and ends after `eventQuietSec` (15 ms) below it, or after
`eventMaxSec` (40 ms). It is classified when it ends:

- `SixFaces` (the classic mode, kept for other uses): the face is the axis with the largest jerk at the peak sample, with
  the sign of that component; the peak is the jerk norm. Index order: +X, -X, +Y, -Y, +Z, -Z.
- `TwoFaces` (the test firmware): only the two calibrated faces count. For each calibrated axis the first lobe is tracked,
  a run of samples whose jerk on that axis is at least `candidateGPerS` (it ends at the first sample below). The axis whose
  first lobe *starts earlier* decides. Equal starts go to the larger peak. Its sign must match the calibrated face of that
  axis; otherwise the candidate is `other_face`. A candidate with no lobe on either calibrated axis (a side edge, a
  diagonal knock) is `other_face` too.

Why the earliest lobe and not the largest one: the case rings after the impact. In the test firmware's data (summary of
`tools/tilt-scroll-test/user_session.log`) the largest-jerk rule put the bottom edge on +Y, because the ringing of the case
was larger than the knock on the bottom axis. The impact is always the first lobe; ringing comes after it, so the earliest
lobe keeps the top knock on its face. Tests `ringing_keeps_top_face` and `back_knock_ringing_keeps_back_face` check this.

### Acceptance

An accepted event passes these checks in order; the first failing one gives the rejection reason:

1. `refractory`: the event starts within `refractorySec` (80 ms) of the last accepted tap. This is the ringing of the
   previous knock, not a second tap.
2. `moving`: the mean jerk norm before the event (`activity`, an exponential average of the jerk norm with
   `activityTauSec` = 0.2 s) is at least `movingGPerS` (12 g/s), or `burstEvents` (3) candidate events started within
   `burstWindowSec` (0.3 s). A sustained high-passed energy (over about 0.2 s) or a burst of candidates counts; hand sway
   does not, because its high-passed jerk is a few g/s.
3. `other_face` (TwoFaces only): see above.
4. `weak`: the peak is below `faceGPerS[face]`. In TwoFaces the peak is the first lobe's peak.
5. `gyro`: only if `gyroConfirmDps` > 0: the gyroscope norm must reach it during the event.

The activity average starts from rest, so the first knock of a shake can be accepted before the average has risen. The
burst rule removes the rest of the shake, but not that first knock; this is a known limitation.

### Single and double taps

`doubleTapEnabled` selects the behaviour. The library default is `true` (unchanged): an accepted tap waits `doubleWindowSec`
(0.35 s) for a second tap on the same face; if one arrives it is a double tap, otherwise the single tap is reported when the
window expires. The test firmware sets `doubleTapEnabled = false`: a single tap is reported at once (the latency is the
quiet time, about 15 ms after the knock). A double knock then counts as two singles (or as refractory ringing if they are
within 80 ms).

### Queues

`PopTap` and `PopRejection` return the oldest entries of two fixed queues of 8. The newest entry overwrites the oldest when
a queue is full, so the caller must drain them regularly (the test firmware does, every frame).

### Thresholds and the measured floor

The header defaults: `candidateGPerS` = 25 g/s and `faceGPerS` = 25 g/s on every face; `movingGPerS` = 12 g/s;
`highPassHz` = 20 Hz; `SixFaces`; `doubleTapEnabled` = true. The test firmware sets the threshold after calibration as
described in its README (candidate `clamp(0.15 x median, 15 x floor, 0.35 x median)`), and the page level as 3 x median.

The 25 g/s default comes from the measured noise and from the knocks the person made:

- the noise floor (`[NOISE]` line, the rest window, high-passed jerk RMS per axis) was 0.8 g/s on the last boot and 0.8 to
  1.2 g/s in the session log; 25 g/s is about 20 to 30 times that;
- the session log's 58 `weak` rejections (with a face threshold of 60 g/s) all had peaks between 32 and 67 g/s, so the
  person's light knocks were above 30 g/s. A threshold of 25 g/s lets all of them through the level check; the 60 g/s face
  threshold was the single biggest reason for lost knocks;
- the 15 x floor lower bound and the 35 % of median upper bound: the session's median accepted peak (59 accepted taps in the
  whole log, median 187 g/s) gives 0.15 x 187 = 28 g/s, inside the bounds, so the calibration lands close to the default.

The `moving` threshold of 12 g/s was set against the rest activity (1.2 g/s on the last boot, 1.8 g/s in the session log) and
the sway model (mean high-passed jerk about 3.6 g/s at 8 Hz, 0.3 g). 12 g/s is about three times the sway and about ten times
the rest.

The 80 ms refractory window was kept. The session log has no timestamps, so the 26 `refractory` rejections (12 in the first
557 lines) cannot be timed against it; the 80 ms value could not be checked and stays as it was.

## Files

- `VHAL/Common/Utilities/Hardware/Motion/TiltScroll.h`
- `VHAL/Common/Utilities/Hardware/Motion/TapDetector.h`
- `tools/vhal-host-tests/motion/tests/motion_tests.cpp`: the host tests (`[HOST] motion.*`)
- `tools/tilt-scroll-test/`: the on-board test (its README has the procedure)
