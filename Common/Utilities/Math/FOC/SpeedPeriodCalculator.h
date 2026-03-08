#pragma once
#include <VHAL.h>


template<typename T = float>
class SpeedPeriodCalculator {
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
    	T speedRpm = 0;      	// speed in r.p.m.
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
		out.speedRpm = parameters.baseRpm * out.speed;

		return *this;
	}


    Out Get() {
    	return out;
    }
};
