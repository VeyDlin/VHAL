#pragma once
#include <cmath>
#include <functional>



template <typename Type = float>
class PidController {
    static_assert((
                std::is_same<Type, float>::value ||
                std::is_same<Type, double>::value ||
                std::is_same<Type, long double>::value
            ), "template instantiation has an invalid type"
        );

public:
    struct Inpit {
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
    Inpit inpit;
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





    PidController& SetInpit(Inpit val) {
    	val.reference = ClampReferenceDeadZone(val.reference);
        inpit = val;
        return *this;
    }
    PidController& SetReference(Type reference) {
        inpit.reference = ClampReferenceDeadZone(reference);
        return *this;
    }
    PidController& SetFeedback(Type feedback, uint32 frequency) {
        inpit.feedback = feedback;
        inpit.frequency = frequency;
        return *this;
    }
    PidController& SetFeedback(Type feedback) {
        inpit.feedback = feedback;
        return *this;
    }
    PidController& SetFrequency(uint32 val) {
        inpit.frequency = val;
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
    	float samplingTimeMs = (1.0f / (float)inpit.frequency) * 1000.0f;
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
			error = inpit.feedback - inpit.reference;
		} else {
			error = inpit.reference - inpit.feedback;
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

			// Если мы зашли в мертвую зону то код ниже все испортит
			// По этому возвращаем обычную ошибку
			if (inpit.feedback >= rollDeadZone.start && inpit.feedback <= rollDeadZone.end) {
				return error;
			}

			// В какой точке стремится оказаться ПИД (опираясь на ошибку)
			float endFeedback = error + inpit.feedback;

			// Отрезок который хочет пройти ПИД
			float pidPathStart = std::min(endFeedback, inpit.feedback);
			float pidPathEnd = std::max(endFeedback, inpit.feedback);

			// Отрезок мертвой зоны
			float deadZoneStart = rollDeadZone.start;
			float deadZoneEnd = rollDeadZone.end;

			// Если отрезок пути ПИД пересекает нулевую точку (точку совмещения начала и кона Roll)
			// То мы коректируем координаты
			if (pidPathEnd > calculatedValues.fullRoll) {
				deadZoneStart += calculatedValues.fullRoll;
				deadZoneEnd += calculatedValues.fullRoll;
			}

			// Пересекается ли предпологаемый путь ПИД и отрезок мертвой зоны
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

        // Error
        Type error = GetError();

        // Proportional
        save.output = error * coefficients.proportional;


        // Integrator
        if (coefficients.integral != 0) {
            save.integrator += (error * coefficients.integral) / inpit.frequency;
            if (integratorLimit.enable) {
                save.integrator = std::min(std::max(save.integrator, integratorLimit.min), integratorLimit.max);
            }
            save.output += save.integrator;
        }


        // Derivative
        if (coefficients.derivative != 0) {
            save.output += coefficients.derivative * (error - save.error) * inpit.frequency;
        }


        // Output
        save.output = std::min(std::max(save.output, output.min), output.max);


        // Last error
        save.error = error;
    }





    void ResolveFilter() {

        // Time sampling
        Type deltaTimeSampling = (Type)1.0 / inpit.frequency;


        // Error
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
