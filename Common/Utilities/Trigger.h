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


// Used to monitor changes in a variable's value.
// The condition triggers only once when the monitored variable changes.
// Example usage:
/*
    int value = 10;
    auto observer = TriggerObserver<int>(value); // Pass a reference to `value`
    if (observer) { 
        // Executes only once when `value` changes.
    }

    while (true) {
        // if (observer)  - Waits for `value` to change.
                            Equivalent to: observer.Event()

        // if (!observer) - Executes every time except when `value` changes.
                            Equivalent to: !observer.Event()
        if (observer) { 
                        
            // Executes only once when the current `value` is not equal to the last checked value.
        }
    }
    value = 20; // Changes the monitored variable. After this, the condition above will execute.
*/
template<typename Type>
class TriggerObserver {
private:
	Type lastData;
	Type* data = nullptr;

public:
	TriggerObserver() { }

	TriggerObserver(Type &val): lastData(val), data(&val) { }

	operator bool() const {
		return Event();
	}

    bool operator!() const {
        return !Event();
    }

	bool Event() {
		if(lastData != *data) {
			lastData = *data;
			return true;
		}

		return false;
	}
};


// Used to detect rising edge of a condition.
// The condition triggers only once when it changes from false to true,
// and will not trigger again until it returns to false first.
//
// Example usage:
/*
   EdgeTrigger trigger;
   while (true) {
       bool condition = SomeErrorCheck();
       trigger = condition; // Pass the external condition each loop
       if (trigger) {
           // Executes only once when condition becomes true (rising edge).
           // Will not trigger again until condition becomes false, then true again.
       }
   }

   // Check current internal state:
   if (trigger == true) { } // Equivalent to trigger.GetState() == true
   if (trigger == false) { } // Equivalent to trigger.GetState() == false
*/
class EdgeTrigger {
private:
	bool lastState = false;
	bool currentState = false;

public:
	EdgeTrigger() { }

	EdgeTrigger(bool initial) :
		lastState(initial), currentState(initial) {
	}

	void operator=(bool condition) {
		lastState = currentState;
		currentState = condition;
	}

	operator bool() const {
		return currentState && !lastState;
	}

	bool operator!() const {
		return !currentState || lastState;
	}

	bool operator==(bool val) const {
		return currentState == val;
	}

	bool operator!=(bool val) const {
		return currentState != val;
	}

	bool GetState() const {
		return currentState;
	}
};
