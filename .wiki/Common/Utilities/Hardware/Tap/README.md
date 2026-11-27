# Tap

`TapDetector` turns knocks on a hand-held device into taps: one on a calibrated edge, one on a calibrated back face (or on
six faces). It is a library of its own, next to `Hardware/Buttons`, and it uses no VHAL adapter, RTOS or UI library, so the
host tests compile it as it is. The header is `Common/Utilities/Hardware/Tap/TapDetector.h`; it also contains the one-pole
high-pass filter `AccelHighPass` that the detector uses.

Input: `Sample` with a microsecond `timestampUs`, accelerometer `accelX/Y/Z` (g) and gyroscope `gyroX/Y/Z` (deg/s). The
MPU6886 driver (`Drivers/Sensor/MPU6886`) produces the samples at 1 kHz from its FIFO on the M5StickC PLUS2 (see its page).
Use consecutive samples at a fixed rate: a tap is 5 to 20 ms, so 100 Hz misses most of them.

Number types: `float` only. `TapDetector` is not a template: its inputs are float IMU readings with a float time step in seconds
(from the microsecond timestamps), and its thresholds and the exponential average of the activity are float tuning values. IQ
has `sqrt` and `abs`, so the reason is not the functions. Converting would change the tuned behaviour, and the thresholds would
need re-checking on the board first.

Host tests: `Utilities/Hardware/Tap/.test/TestTapDetector.cpp` (`./test.cmd Tap`), on synthetic traces.

---

## Table of Contents

- [Sources](#sources)
- [High-pass Acceleration and Jerk](#high-pass-acceleration-and-jerk)
- [Event and Classification](#event-and-classification)
- [Acceptance](#acceptance)
- [Single and Double Taps](#single-and-double-taps)
- [Queues](#queues)
- [Thresholds and the Measured Floor](#thresholds-and-the-measured-floor)
- [Files](#files)

---

## Sources

Designs and defaults come from the literature below. The numbers in the code are starting points, not measured results,
except where a section says it was measured on the board.

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

## High-pass Acceleration and Jerk

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

## Event and Classification

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
lobe keeps the top knock on its face. The checks `CheckRingingKeepsTopFace` and
`CheckBackKnockRingingKeepsBackFace` in `TestTapDetector.cpp` check this.

## Acceptance

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

## Single and Double Taps

`doubleTapEnabled` selects the behaviour. The library default is `true` (unchanged): an accepted tap waits `doubleWindowSec`
(0.35 s) for a second tap on the same face; if one arrives it is a double tap, otherwise the single tap is reported when the
window expires. The test firmware sets `doubleTapEnabled = false`: a single tap is reported at once (the latency is the
quiet time, about 15 ms after the knock). A double knock then counts as two singles (or as refractory ringing if they are
within 80 ms).

## Queues

`PopTap` and `PopRejection` return the oldest entries of two fixed queues of 8. The newest entry overwrites the oldest when
a queue is full, so the caller must drain them regularly (the test firmware does, every frame).

## Thresholds and the Measured Floor

The header defaults: `candidateGPerS` = 25 g/s and `faceGPerS` = 25 g/s on every face; `movingGPerS` = 12 g/s;
`highPassHz` = 20 Hz; `SixFaces`; `doubleTapEnabled` = true. The test firmware sets the threshold after calibration as
described in `tools/tilt-scroll-test/README.md` (candidate `clamp(0.15 x median, 15 x floor, 0.35 x median)`), and the page
level as 3 x median.

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

---

## Files

- `VHAL/Common/Utilities/Hardware/Tap/TapDetector.h`: the detector and `AccelHighPass`.
- `Utilities/Hardware/Tap/.test/TestTapDetector.cpp`: the host tests (`Tap.TestTapDetector`).
- `tools/tilt-scroll-test/`: the on-board test that uses the detector (its README has the procedure).
