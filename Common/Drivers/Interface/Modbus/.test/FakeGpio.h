#pragma once
#include <VHAL.h>
#include <vector>


// A GPIO pin of the simulator, with the Set and Reset calls of GPIOAdapter. Every level change is logged with the simulated time
// of the change, so a test can check the order and the times of DE and RE against the frame.
class FakeGpio {
public:
	struct Change {
		uint64 timeUs;
		bool level;
	};


private:
	bool level = false;
	std::vector<Change> changes;


public:
	FakeGpio& Set() {
		Write(true);
		return *this;
	}


	FakeGpio& Reset() {
		Write(false);
		return *this;
	}


	bool Level() const {
		return level;
	}


	const std::vector<Change>& Changes() const {
		return changes;
	}


private:
	void Write(bool value) {
		level = value;
		changes.push_back(Change{simNowUs, value});
	}
};
