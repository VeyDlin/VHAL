#pragma once


// Single-shot trigger flag: fires once when state matches, then auto-inverts
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
