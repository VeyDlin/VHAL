#pragma once
#include <BSP.h>

/*
 * Adaptive hysteresis filter
 
 * This class implements a hysteresis-like behavior with adjustable window boundaries
 * It maintains a range (window) defined by a "half-gap" around the current value.
 * The window adjusts dynamically when the input value crosses its boundaries,
 * optionally allowing directional "pulling" behavior
 
 * Behavior is controlled via the PullType enum:
 * - NoPull: Symmetrically adjusts the window when the input value moves outside it
 * - PullUp: Adjusts the window only when the input value increases
 * - PullDown: Adjusts the window only when the input value decreases
 
 * This class is useful for filtering noisy input signals or creating hysteresis effects
 
 * Example usage:
	Hysteresis<float> hysteresis(5.0, Hysteresis<float>::PullType::NoPull);
	float filteredValue = hysteresis.Get(10.0);
*/

template <typename ValueType = float>
class Hysteresis {
public:
    // Enum defining the pulling behavior of the window
    enum class PullType {
        NoPull,  // Adjust both bounds symmetrically when the value moves outside the window
        PullUp,  // Adjust only the upper bound if the value increases
        PullDown // Adjust only the lower bound if the value decreases
    };

private:
    ValueType lastValue = 0;         // Last confirmed value
    ValueType bounds[2] = {0, 0};    // Window bounds: [lowerBound, upperBound]
    ValueType halfGap;               // Half of the gap (range size / 2)
    PullType pull;                   // Pull behavior (e.g., NoPull, PullUp, PullDown)

public:
    Hysteresis() = default;

    Hysteresis(ValueType setHalfGap, PullType setPull = PullType::NoPull): halfGap(setHalfGap), pull(setPull) {
        SystemAssert(halfGap >= 0);
    }


    // Processes a new value and adjusts the window if necessary
    // return The filtered value (last confirmed value within the adjusted window)
    ValueType Get(ValueType value) {
        // Initialize window on the first call
        if (bounds[0] == bounds[1]) {
            bounds[0] = value - halfGap;
            bounds[1] = value + halfGap;
            lastValue = value;
            return lastValue;
        }

        // Adjust the window and lastValue based on the pull behavior
        if (value > bounds[1] || (pull == PullType::PullUp && value > lastValue)) {
            UpdateBounds(value, true);
        } else if (value < bounds[0] || (pull == PullType::PullDown && value < lastValue)) {
            UpdateBounds(value, false);
        }

        return lastValue;
    }

private:
    void UpdateBounds(ValueType value, bool isUpperPull) {
        if (isUpperPull) {
            bounds[1] = value;
            bounds[0] = value - halfGap * 2;
        } else {
            bounds[0] = value;
            bounds[1] = value + halfGap * 2;
        }
        lastValue = value;
    }
};