#pragma once
#include <VHAL.h>
#include <Utilities/Math/NumberRange.h>
#include <type_traits>


// Speed from the timer counts between two events. The counts are timer values up to 0x7FFF (the default
// maximumOutput), so T must hold 32767: float, or IQ<Q> with Q <= 16. IQ<17> and above fail to compile.
// The speed is in T. The rpm is baseRpm * speed: a float rpm, or for IQ an int32 rpm computed from the raw values in 64
// bits and truncated toward zero, so a Q16 rpm above 32767 does not saturate (46800 rpm at 26 pu is an example).
template<typename T = float> requires RangeAtLeast<T, 32767.0>
class SpeedPeriodCalculator {
public:
    // The rpm type: T for float, int32 for IQ.
    using RpmType = std::conditional_t<std::is_floating_point_v<T>, T, int32>;


private:
	T newTimeStamp = 0;  		// New 'Timestamp' corresponding to a capture event
    T oldTimeStamp = 0;  		// Old 'Timestamp' corresponding to a capture event

    struct {
		T timeStamp = 0; 	   	// Current 'Timestamp' corresponding to a capture event
		T eventPeriod = 0;    	// Event Period
    } in;


public:
    enum class InputSelect { TimeStamp , EventPeriod };

    struct {
        T speedScaler = 260;    // Scaler converting 1/N cycles to a speed
        T baseRpm = 1800;       // Scaler converting GLOBAL_Q speed to rpm speed
    	InputSelect inputSelect = InputSelect::TimeStamp;    // Input selection between TimeStamp (InputSelect=0) and EventPeriod (InputSelect=1)
        T maximumOutput = 0x7FFF;
    } parameters;


    struct Out {
    	T speed = 0;         	// speed in per-unit
    	RpmType speedRpm = 0;  	// speed in r.p.m. (T for float, int32 for IQ)
    } out;



    SpeedPeriodCalculator() {}

    SpeedPeriodCalculator(T speedScaler, T baseRpm, T maximumOutput, InputSelect inputSelect = InputSelect::TimeStamp) {
    	parameters.speedScaler = speedScaler;
    	parameters.baseRpm = baseRpm;
    	parameters.inputSelect = inputSelect;
    	parameters.maximumOutput = maximumOutput;
    }


    SpeedPeriodCalculator& Reset() {
    	out.speed = 0;
    	out.speedRpm = 0;
    	return *this;
    }


    SpeedPeriodCalculator& Set(T timeStamp, T eventPeriod) {
    	in.timeStamp = timeStamp;
    	in.eventPeriod = eventPeriod;
    	return *this;
    }


    SpeedPeriodCalculator& SetTimeStamp(T timeStamp) {
    	in.timeStamp = timeStamp;
    	return *this;
    }


    SpeedPeriodCalculator& SetEventPeriod(T eventPeriod) {
    	in.eventPeriod = eventPeriod;
    	return *this;
    }


    SpeedPeriodCalculator& Resolve() {
		if(parameters.inputSelect == InputSelect::TimeStamp) {
			oldTimeStamp = newTimeStamp;
			newTimeStamp = in.timeStamp;
			in.eventPeriod = newTimeStamp - oldTimeStamp;

			if(in.eventPeriod < 0) {
				in.eventPeriod += parameters.maximumOutput;
			}
		}

		out.speed = parameters.speedScaler / in.eventPeriod;

		// The rpm product is taken in 64 bits for IQ (raw * raw has 2Q fractional bits); the division by 2^(2Q) truncates
		// toward zero. The float product is unchanged.
		if constexpr (std::is_floating_point_v<T>) {
			out.speedRpm = parameters.baseRpm * out.speed;
		} else {
			int64 product = static_cast<int64>(parameters.baseRpm.raw) * static_cast<int64>(out.speed.raw);
			out.speedRpm = static_cast<int32>(product / (int64(1) << (2 * T::q)));
		}

		return *this;
	}


    Out Get() {
    	return out;
    }
};
