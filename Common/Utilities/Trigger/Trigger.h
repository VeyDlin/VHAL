#pragma once


// Used for single-condition checks when setting a flag.
// If the condition is met, the flag is automatically inverted.
// The condition triggers only once when the current state matches the expected state, then changes.
// Example usage:
/*
    Trigger flag = false; // Default value
    while (true) {
        // if (flag)  - Waits for the flag to become true
                        Equivalent to: flag.Is(true)

        // if (!flag) - Waits for the flag to become false
                        Equivalent to: flag.Is(false)
        if (flag) {               
            // Executes only once if the current state == true.
            // After execution (condition triggers), the flag switches to false.
            // To trigger the condition again, you need to set the flag back to true.
        }
    }
    flag = true; // Explicitly sets the flag. After this, the condition above will execute.

    // Retrieve the current state of the flag without triggering:
    if (flag == true) { } // Equivalent to flag.GetState() == true
    if (flag == false) { } // Equivalent to flag.GetState() == false
*/
class Trigger {
private:
	bool trigger = false;

public:
	Trigger() { }

	Trigger(bool val): trigger(val) { }

    operator bool() const {
        return const_cast<Trigger*>(this)->Is(true);
    }

    bool operator!() const {
        return const_cast<Trigger*>(this)->Is(false);
    }

    bool operator==(bool val) const {
        return trigger == val;
    }

    bool operator!=(bool val) const {
        return trigger != val;
    }

	void operator=(bool val) {
		trigger = val;
	}

    bool GetState() const {
        return trigger;
    }

	bool Is(bool wait) {
		if(trigger == wait) {
			trigger = !trigger;
			return true;
		}
		return false;
	}
};
