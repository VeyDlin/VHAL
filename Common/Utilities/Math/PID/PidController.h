#pragma once
#include <cmath>
#include <functional>

/*
// Creates a PID controller with default parameters.
  PidController<> pid;

// Creates a PID controller with specified coefficients and output limits.
  PidController<> pid({1.0f, 0.5f, 0.1f}, {-10.0f, 10.0f});

// Performs PID calculation.
  pid.Resolve();

// Resets the controller state.
  pid.Reset();

// Sets the input data.
  pid.SetInput({350.0f, 10.0f, 50});

// Sets the target value.
  pid.SetReference(15.0f);

// Sets the feedback and frequency.
  pid.SetFeedback(12.0f, 100);

// Sets only the feedback.
  pid.SetFeedback(8.0f);

// Sets the PID coefficients.
  pid.SetCoefficients({1.0f, 0.5f, 0.1f});

// Sets the proportional coefficient.
  pid.SetProportional(1.0f);

// Sets the integral coefficient.
  pid.SetIntegral(0.5f);

// Sets the derivative coefficient.
  pid.SetDerivative(0.1f);

// Sets filtering parameters.
  pid.SetFilter({true, 0.8f, 0.2f});

// Enables or disables filtering.
  pid.SetFilterEnable(true);

// Sets the integrator saturation coefficient.
  pid.SetFilterBackSaturation(0.8f);

// Sets the filtering coefficient for the derivative component.
  pid.SetFilterDerivative(0.2f);

// Configures the stabilization event.
  pid.SetStabilizedEvent({
      .enable = true,
      .errorMin = -0.1f,
      .errorMax = 0.1f,
      .timeMs = 500,
      .onStabilized = [](PidController<>& self) {
          std::cout << "Stabilization achieved. Output: " << self.Get() << "\n";
      }
  });

// Configures the destabilization event.
  pid.SetDestabilizedEvent({
      .enable = true,
      .errorMin = -0.5f,
      .errorMax = 0.5f,
      .timeMs = 300,
      .onDestabilized = [](PidController<>& self) {
          std::cout << "Destabilization. Error: " << self.GetLastError() << "\n";
      }
  });

// Returns the current output value.
  float output = pid.Get();

// Returns the last error value.
  float lastError = pid.GetLastError();
*/


/*
====== Input Structure ======
Defines the input parameters for the PID controller.

Fields:
  feedback (Type): The current feedback value (e.g., system position).
  reference (Type): The target value the system aims for.
  frequency (uint32): Sampling frequency in Hz (frequency of calling `Resolve`).

Usage example:
  pid.SetInput({350.0f, 10.0f, 50}); // Angle 350°, target angle 10°, frequency 50 Hz


====== Output Structure ======
Defines the output parameters for the PID controller.

Fields:
  max (Type): Maximum output signal value.
  min (Type): Minimum output signal value.
  inversion (bool): If true, inverts the sign of the output signal.

Usage example:
  pid.SetOutput({10.0f, -10.0f}); // Output limit from -10 to 10


====== Coefficients Structure ======
Defines the PID controller coefficients.

Fields:
  proportional (Type): Proportional coefficient (P).
  integral (Type): Integral coefficient (I).
  derivative (Type): Derivative coefficient (D).

Usage example:
  pid.SetCoefficients({1.0f, 0.5f, 0.1f}); // Proportional = 1.0, Integral = 0.5, Derivative = 0.1


====== IntegratorLimit Structure ======
Defines the limits for the integral component.

Fields:
  enable (bool): Enables the integrator limit.
  max (Type): Maximum value of the integral component.
  min (Type): Minimum value of the integral component.

Usage example:
  pid.SetIntegratorLimit({true, 5.0f, -5.0f}); // Limit from -5 to 5, enabled


====== Roll Structure ======
Defines parameters for cyclic data (e.g., angular systems).

Fields:
  enable (bool): Enables cyclic data support.
  maxInput (Type): Maximum input data value (e.g., 360°).
  minInput (Type): Minimum input data value (e.g., 0°).

Usage example:
  pid.SetRoll({true, 360.0f, 0.0f}); // Cyclic support enabled, range from 0 to 360 degrees


====== RollDeadZone Structure ======
Defines parameters for the "dead zone" in cyclic data.

Fields:
  enable (bool): Enables the "dead zone".
  throughConnection (bool): If true, allows through-connection through the "dead zone".
  start (Type): Start of the "dead zone" range.
  end (Type): End of the "dead zone" range.

Usage example:
  pid.SetRollDeadZone({true, false, 85.0f, 95.0f}); // Dead zone from 85° to 95° without through-connection


====== Filter Structure ======
Defines filtering parameters.

Fields:
  enable (bool): Enables filtering.
  backSaturation (Type): Saturation coefficient for the integral component.
  derivative (Type): Filtering coefficient for the derivative component.

Usage example:
  pid.SetFilter({true, 0.8f, 0.2f}); // Filtering enabled, saturation coefficient 0.8, filtering coefficient 0.2


====== StabilizedEvent Structure ======
Defines stabilization event parameters.

Fields:
  enable (bool): Enables the stabilization event.
  errorMin (Type): Minimum error to define stable state.
  errorMax (Type): Maximum error to define stable state.
  timeMs (uint32): Time in milliseconds the error must stay within the range.
  onStabilized (std::function<void(PidController&)>): Stabilization event handler.

Usage example:
  pid.SetStabilizedEvent({
      .enable = true,
      .errorMin = -0.1f,
      .errorMax = 0.1f,
      .timeMs = 500,
      .onStabilized = [](PidController<>& self) {
          std::cout << "Stabilization achieved. Output: " << self.Get() << "\n";
      }
  });


====== DestabilizedEvent Structure ======
Defines destabilization event parameters.

Fields:
  enable (bool): Enables the destabilization event.
  errorMin (Type): Minimum error to define destabilized state.
  errorMax (Type): Maximum error to define destabilized state.
  timeMs (uint32): Time in milliseconds the error must stay outside the range.
  onDestabilized (std::function<void(PidController&)>): Destabilization event handler.

Usage example:
  pid.SetDestabilizedEvent({
      .enable = true,
      .errorMin = -0.5f,
      .errorMax = 0.5f,
      .timeMs = 300,
      .onDestabilized = [](PidController<>& self) {
          std::cout << "Destabilization. Error: " << self.GetLastError() << "\n";
      }
  });
*/


template <typename Type = float>
class PidController {
    static_assert((
                std::is_same<Type, float>::value ||
                std::is_same<Type, double>::value ||
                std::is_same<Type, long double>::value
            ), "template instantiation has an invalid type"
        );

public:
    struct Input {
        Type feedback = 0;
        Type reference = 0;
        uint32 frequency = 1;
    };

    struct Output {
        Type max = 1;
        Type min = 0;
        bool inversion = false;
    };

    struct Coefficients {
        Type proportional = 1;
        Type integral = 0;
        Type derivative = 0;
    };

    struct IntegratorLimit {
        bool enable = false;
        Type max = 1;
        Type min = 0;
    };

    struct Roll {
        bool enable = false;
        Type maxInput = 1;
        Type minInput = 0;
    };

    struct RollDeadZone {
        bool enable = false;
        bool throughConnection = false;
        Type start = 0;
        Type end = 0;
    };

    struct Filter {
        bool enable = false;
        Type backSaturation = 1;
        Type derivative = 1;
    };

    struct StabilizedEvent {
        bool enable = false;
        Type errorMax = 1;
        Type errorMin = 0;
        Type timeMs = 100;
        std::function<void(PidController& self)> onStabilized = nullptr;
    };

    struct DestabilizedEvent {
        bool enable = false;
        Type errorMax = 1;
        Type errorMin = 0;
        Type timeMs = 100;
        std::function<void(PidController& self)> onDestabilized = nullptr;
    };

private:
    Input input;
    Output output;
    Coefficients coefficients;
    IntegratorLimit integratorLimit;
    Roll roll;
    RollDeadZone rollDeadZone;
    Filter filter;
    StabilizedEvent stabilizedEvent;
    DestabilizedEvent destabilizedEvent;


    struct {
        Type integrator = 0;
        Type error = 0;
        Type output = 0;
        Type lastOutput = 0;
        Type derivative = 0;
        Type integralFilter = 0;
        Type derivativeFilter = 0;
        uint32 stabilizedTime = 0;
        uint32 destabilizedTime = 0;
    } save;

    struct {
        Type fullRoll;
        Type halfRoll;
    } calculatedValues;

    struct {
        bool frozen = false;
        bool isStabilized = false;
        bool isDestabilized = false;
    } state;


public:
    PidController() {}

    PidController(Coefficients _coefficients, Output _output) {
        coefficients = _coefficients;
        output = _output;
    }

    PidController& Resolve() {
        if (state.frozen) {
            return *this;
        }

        if (filter.enable) {
            ResolveFilter();
        } else {
            ResolveNormal();
        }

        ProcessEvents();

        return *this;
    }

    PidController& Reset() {
        save.integrator = 0;
        save.error = 0;
        save.output = 0;
        save.lastOutput = 0;
        save.derivative = 0;
        save.integralFilter = 0;
        return *this;
    }

    PidController& SetInpit(Input val) {
    	val.reference = ClampReferenceDeadZone(val.reference);
        input = val;
        return *this;
    }

    PidController& SetReference(Type reference) {
        input.reference = ClampReferenceDeadZone(reference);
        return *this;
    }

    PidController& SetFeedback(Type feedback, uint32 frequency) {
        input.feedback = feedback;
        input.frequency = frequency;
        return *this;
    }

    PidController& SetFeedback(Type feedback) {
        input.feedback = feedback;
        return *this;
    }

    PidController& SetFrequency(uint32 val) {
        input.frequency = val;
        return *this;
    }

    PidController& SetRoll(Roll val) {
        roll = val;
        calculatedValues.fullRoll = roll.maxInput + std::abs(roll.minInput);
        calculatedValues.halfRoll = calculatedValues.fullRoll / (Type)2;
        return *this;
    }

    PidController& SetRollEnable(bool val) {
        roll.enable = val;
        return *this;
    }

    PidController& SetRollDeadZone(RollDeadZone val) {
        rollDeadZone.enable = val.enable;
        rollDeadZone.throughConnection = val.throughConnection;
        rollDeadZone.start = std::min(val.start, val.end);
        rollDeadZone.end = std::max(val.start, val.end);
        return *this;
    }

    PidController& SetStabilizedEvent(StabilizedEvent val) {
    	stabilizedEvent = val;
        return *this;
    }

    PidController& SetStabilizedEventEnable(bool val) {
    	stabilizedEvent.enable = val;
        return *this;
    }

    PidController& SetStabilizedEventTime(uint32 val) {
    	stabilizedEvent.timeMs = val;
        return *this;
    }

    PidController& SetStabilizedEventMin(Type val) {
    	stabilizedEvent.errorMin = val;
        return *this;
    }

    PidController& SetStabilizedEventMax(Type val) {
    	stabilizedEvent.errorMax = val;
        return *this;
    }

    PidController& SetStabilizedEventMinMax(Type val) {
    	stabilizedEvent.errorMin = -val;
    	stabilizedEvent.errorMax = val;
        return *this;
    }

    PidController& SetStabilizedEventMinMax(Type min, Type max) {
    	stabilizedEvent.errorMin = min;
    	stabilizedEvent.errorMax = max;
        return *this;
    }

    PidController& SetStabilizedEventHandle(std::function<void(PidController& self)> val) {
    	stabilizedEvent.onStabilized = val;
        return *this;
    }

    PidController& SetDestabilizedEvent(DestabilizedEvent val) {
    	destabilizedEvent = val;
        return *this;
    }

    PidController& SetDestabilizedEventEnable(bool val) {
    	destabilizedEvent.enable = val;
        return *this;
    }

    PidController& SetDestabilizedEventTime(uint32 val) {
    	destabilizedEvent.timeMs = val;
        return *this;
    }

    PidController& SetDestabilizedEventMin(Type val) {
    	destabilizedEvent.errorMin = val;
        return *this;
    }

    PidController& SetDestabilizedEventMax(Type val) {
    	destabilizedEvent.errorMax = val;
        return *this;
    }

    PidController& SetDestabilizedEventMinMax(Type val) {
    	destabilizedEvent.errorMin = -val;
    	destabilizedEvent.errorMax = val;
        return *this;
    }

    PidController& SetDestabilizedEventMinMax(Type min, Type max) {
    	destabilizedEvent.errorMin = min;
    	destabilizedEvent.errorMax = max;
        return *this;
    }

    PidController& SetDestabilizedEventHandle(std::function<void(PidController& self)> val) {
    	destabilizedEvent.onDestabilized = val;
        return *this;
    }

    PidController& SetCoefficients(Coefficients val) {
        coefficients = val;
        return *this;
    }

    PidController& SetProportional(Type val) {
        coefficients.proportional = val;
        return *this;
    }

    PidController& SetIntegral(Type val) {
        coefficients.integral = val;
        return *this;
    }

    PidController& SetDerivative(Type val) {
        coefficients.derivative = val;
        return *this;
    }

    PidController& SetFilter(Filter val) {
        filter = val;
        return *this;
    }

    PidController& SetFilterEnable(bool val) {
        filter.enable = val;
        return *this;
    }

    PidController& SetFilterBackSaturation(Type val) {
        filter.backSaturation = val;
        return *this;
    }
    
    PidController& SetFilterDerivative(Type val) {
        filter.derivative = val;
        return *this;
    }

    PidController& SetIntegratorLimit(IntegratorLimit val) {
        integratorLimit = val;
        return *this;
    }

    PidController& SetIntegratorLimit(Type val) {
        val = std::abs(val);
        integratorLimit.max = val;
        integratorLimit.min = -val;
        return *this;
    }

    PidController& SetIntegratorLimitEnable(bool val) {
        integratorLimit.enable = val;
        return *this;
    }

    PidController& SetIntegratorLimitMin(Type val) {
        integratorLimit.min = val;
        return *this;
    }

    PidController& SetIntegratorLimitMax(Type val) {
        integratorLimit.max = val;
        return *this;
    }

    PidController& SetIntegratorLimitMinMax(Type val) {
    	integratorLimit.min = -val;
        integratorLimit.max = val;
        return *this;
    }

    PidController& SetIntegratorLimitMinMax(Type min, Type max) {
    	integratorLimit.min = min;
        integratorLimit.max = max;
        return *this;
    }

    PidController& SetOuput(Output val) {
        output = val;
        return *this;
    }

    PidController& SetOuputInversion(bool enableInversion) {
        output.inversion = enableInversion;
        return *this;
    }

    PidController& SetPidOutputValue(Type val) {
        save.output = val;
        return *this;
    }

    PidController& SetFrozen(bool val) {
        state.frozen = val;
        return *this;
    }

    Type Get() {
        return save.output;
    }

    Type GetLastError() {
        return save.error;
    }


private:
    void ProcessEvents() {
    	float samplingTimeMs = (1.0f / (float)input.frequency) * 1000.0f;
    	StabilizedEventProcess(samplingTimeMs);
    	DestabilizedEventProcess(samplingTimeMs);
    }


    void StabilizedEventProcess(float samplingTimeMs) {
        if (!stabilizedEvent.enable) {
        	return;
        }

		if (save.error >= stabilizedEvent.errorMin && save.error <= stabilizedEvent.errorMax) {
			if (!state.isStabilized) {
				save.stabilizedTime++;
				if (samplingTimeMs * save.stabilizedTime >= stabilizedEvent.timeMs) {
					state.isStabilized = true;
					if (stabilizedEvent.onStabilized != nullptr) {
						stabilizedEvent.onStabilized(*this);
					}
				}
			}
		} else {
			save.stabilizedTime = 0;
			state.isStabilized = false;
		}
    }


    void DestabilizedEventProcess(float samplingTimeMs) {
        if (!destabilizedEvent.enable) {
        	return;
        }
		if (save.error >= destabilizedEvent.errorMax || save.error <= destabilizedEvent.errorMin) {
			if (!state.isDestabilized) {
				save.destabilizedTime++;
				if (samplingTimeMs * save.destabilizedTime >= destabilizedEvent.timeMs) {
					state.isDestabilized = true;
					if (destabilizedEvent.onDestabilized != nullptr) {
						destabilizedEvent.onDestabilized(*this);
					}
				}
			}
		} else {
			save.destabilizedTime = 0;
			state.isDestabilized = false;
		}
	}


    Type ClampReferenceDeadZone(Type reference) {
        if (!rollDeadZone.enable) {
        	return reference;
        }


		if (rollDeadZone.throughConnection) {
			if (reference >= roll.minInput && reference <= rollDeadZone.start) {
				return rollDeadZone.start;
			}

			if (reference >= rollDeadZone.end && reference <= roll.maxInput) {
				return rollDeadZone.end;
			}

			return reference;
		}


		if (reference >= rollDeadZone.start && reference <= rollDeadZone.end) {
			if (std::abs(rollDeadZone.start - reference) > std::abs(rollDeadZone.end - reference)) {
				return rollDeadZone.end;
			}

			return rollDeadZone.start;
		}


		return reference;
    }


    Type GetError() {
		float error;
		if (output.inversion) {
			error = input.feedback - input.reference;
		} else {
			error = input.reference - input.feedback;
		}

		if (!roll.enable) {
			return error;
		}

		if (!rollDeadZone.enable || !rollDeadZone.throughConnection) {
			if (error > calculatedValues.halfRoll) {
				error -= calculatedValues.fullRoll;
			} else if (error < -calculatedValues.halfRoll) {
				error += calculatedValues.fullRoll;
			}
		}

		if (rollDeadZone.enable && !rollDeadZone.throughConnection) {
			if (input.feedback >= rollDeadZone.start && input.feedback <= rollDeadZone.end) {
				return error;
			}

			float endFeedback = error + input.feedback;

			float pidPathStart = std::min(endFeedback, input.feedback);
			float pidPathEnd = std::max(endFeedback, input.feedback);

			float deadZoneStart = rollDeadZone.start;
			float deadZoneEnd = rollDeadZone.end;

			if (pidPathEnd > calculatedValues.fullRoll) {
				deadZoneStart += calculatedValues.fullRoll;
				deadZoneEnd += calculatedValues.fullRoll;
			}

			if (deadZoneStart < pidPathEnd && deadZoneEnd > pidPathStart) {
				if (error < roll.minInput) {
					error += calculatedValues.fullRoll;
				} else {
					error -= calculatedValues.fullRoll;
				}
			}
		}

		return error;
	}


    void ResolveNormal() {
        Type error = GetError();
        save.output = error * coefficients.proportional;

        // Integrator
        if (coefficients.integral != 0) {
            save.integrator += (error * coefficients.integral) / input.frequency;
            if (integratorLimit.enable) {
                save.integrator = std::min(std::max(save.integrator, integratorLimit.min), integratorLimit.max);
            }
            save.output += save.integrator;
        }

        // Derivative
        if (coefficients.derivative != 0) {
            save.output += coefficients.derivative * (error - save.error) * input.frequency;
        }

        // Output
        save.output = std::min(std::max(save.output, output.min), output.max);

        // Last error
        save.error = error;
    }


    void ResolveFilter() {
        Type deltaTimeSampling = (Type)1.0 / input.frequency;
        volatile Type error = GetError();

        // Proportional
        Type proportionalComponent = coefficients.proportional * error;

        // Integrator
        if(coefficients.integral != 0) {
            save.integrator += deltaTimeSampling * save.integralFilter;
            if(integratorLimit.enable) {
                save.integrator = std::min(std::max(save.integrator, integratorLimit.min), integratorLimit.max);
            }
            save.integralFilter = (coefficients.integral * error) + (filter.backSaturation * (save.output - save.lastOutput));
        }

        // Derivative
        if(coefficients.derivative != 0) {
            save.derivativeFilter += deltaTimeSampling * save.derivative;
            save.derivative = ((coefficients.derivative * error) - save.derivativeFilter) * filter.derivative;
        }

        // Output
        save.output = proportionalComponent + save.integrator + save.derivative;
        save.lastOutput = save.output;
        save.output = std::min(std::max(save.output, output.min), output.max);

        // Last error
        save.error = error;
    }
};
