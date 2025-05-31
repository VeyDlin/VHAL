#pragma once


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
		return const_cast<TriggerObserver*>(this)->Event();
	}

    bool operator!() const {
    	return !const_cast<TriggerObserver*>(this)->Event();
    }

	bool Event() {
		if(data != nullptr && lastData != *data) {
			lastData = *data;
			return true;
		}

		return false;
	}
};
