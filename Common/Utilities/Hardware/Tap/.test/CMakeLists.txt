# Host tests of the tap detector: knocks on synthetic 1 kHz traces, the face classification, the acceptance checks and the thresholds.

vhal_test(
  GLOB Test*.cpp
  INCLUDES
    "${VHAL_ROOT}/Common")
