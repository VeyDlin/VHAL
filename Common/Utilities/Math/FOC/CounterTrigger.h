#pragma once
#include <BSP.h>



class CounterTrigger {
public:
	enum class Direction : uint8 { Forward = 0, Backward = 1 };

	struct Input {
		bool trigger = false;
		Direction direction = Direction::Forward;
	};

	struct Output {
		uint8 step = 0;	 // Modulo 6 counter output (0, 1, 2, 3, 4, 5)
	};


private:
	Input input;
	Output output;


public:
	CounterTrigger() {}

	CounterTrigger(Direction direction) {
		input.direction = direction;
	}



	CounterTrigger& SetInput(Input val) {
		input = val;
		return *this;
	}



	CounterTrigger& SetTrigger(bool val = true) {
		input.trigger = val;
		return *this;
	}



	CounterTrigger& SetDirection(Direction val) {
		input.direction = val;
		return *this;
	}



	CounterTrigger& Resolve() {
		if (input.trigger) {
			switch (input.direction) {
				case Direction::Forward:
					if (output.step == 5) {
						output.step = 0;
					} else {
						output.step++; 
					}
				break;

				case Direction::Backward:
					if (output.step == 0) {
						output.step = 5;
					} else {
						output.step--;
					}
				break;
			}
		}

		return *this;
	}



	Output Get() {
		return output;
	}

};