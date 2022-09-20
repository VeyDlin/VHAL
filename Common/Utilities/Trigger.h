#pragma once



class Trigger {
private:
	bool trigger = false;

public:
	Trigger() { }
	explicit Trigger(bool val): trigger(val) { }

	bool Is(bool wait) {
		if(trigger == wait) {
			trigger = !trigger;
			return true;
		}
		return false;
	}

	void operator=(bool val) {
		trigger = val;
	}
};




template<typename Type>
class TriggerObserver {
private:
	Type lastData;
	Type* data = nullptr;

public:
	TriggerObserver() { }
	TriggerObserver(Type &val): lastData(val), data(&val) { }

	operator bool() const {
		if(lastData != *data) {
			lastData = *data;
			return true;
		}

		return false;
	}
};






