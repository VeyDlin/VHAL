#pragma once


// Variable change observer: fires once when the monitored value changes
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
