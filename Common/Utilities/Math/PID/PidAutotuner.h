


/*
    auto tuner = PIDAutotuner();
    tuner.setTargetInputValue(targetInputValue);
    tuner.setLoopInterval(loopInterval);
    tuner.setOutputRange(0, 255);
    tuner.setZNMode(PIDAutotuner::Basic);


    long microseconds;
    while (!tuner.isFinished()) {
        long prevMicroseconds = microseconds;
        microseconds = micros();

        double input = doSomethingToGetInput();
        double output = tuner.Tune(input);
        doSomethingToSetOutput(output);

        while (micros() - microseconds < loopInterval) delayMicroseconds(1);
    }


    doSomethingToSetOutput(0);

    double kp = tuner.getKp();
    double ki = tuner.getKi();
    double kd = tuner.getKd();
*/




class PidAutotuner {
public:
    enum class ZieglerNicholsMode { Basic, LessOvershoot, NoOvershoot};

private:
    float targetInputValue = 0;
    float loopInterval = 0;
    float minOutput;
    float maxOutput;
    ZieglerNicholsMode znMode = ZieglerNicholsMode::Basic;
    int cycles = 10;



    int cycleCounter = 0;
    bool output = true;
    float outputValue;
    long t1, t2, tHigh, tLow;

    float maxInput;
    float minInput;

    float pAverage, iAverage, dAverage;

    float kp, ki, kd;


public:

    PidAutotuner() {
        Reset();
    }




    float Tune(float input) {

        // Calculate maxInput and minInput
        maxInput = std::max(maxInput, input);
        minInput = std::min(minInput, input);

        // Output is on and input signal has risen to target
        if (output && input > targetInputValue) {
            // Turn output off, record current time as t1, calculate tHigh, and reset maximum
            output = false;
            outputValue = minOutput;
            t1 = micros();
            tHigh = t1 - t2;
            maxInput = targetInputValue;
        }

        // Output is off and input signal has dropped to target
        if (!output && input < targetInputValue) {
            // Turn output on, record current time as t2, calculate tLow
            output = true;
            outputValue = maxOutput;
            t2 = micros();
            tLow = t2 - t1;

            // Calculate Ku (ultimate gain)
            float ku = (4.0 * ((maxOutput - minOutput) / 2.0)) / (M_PI * (maxInput - minInput) / 2.0);

            // Calculate Tu (period of output oscillations)
            float tu = tLow + tHigh;

            // Calculated
            float kpConstant, tiConstant, tdConstant;

            switch (znMode) {
                case ZieglerNicholsMode::Basic:
                    kpConstant = 0.6;
                    tiConstant = 0.5;
                    tdConstant = 0.125;
                break;

                case ZieglerNicholsMode::LessOvershoot:
                    kpConstant = 0.33;
                    tiConstant = 0.5;
                    tdConstant = 0.33;
                break;

                case ZieglerNicholsMode::NoOvershoot:
                    kpConstant = 0.2;
                    tiConstant = 0.5;
                    tdConstant = 0.33;
                break;
            }

            // Calculate gains
            kp = kpConstant * ku;
            ki = (kp / (tiConstant * tu)) * loopInterval;
            kd = (tdConstant * kp * tu) / loopInterval;

            // Average all gains after the first two cycles
            if (cycleCounter > 1) {
                pAverage += kp;
                iAverage += ki;
                dAverage += kd;
            }

            // Reset minimum
            minInput = targetInputValue;

            // Increment cycle count
            cycleCounter++;
        }

        // If loop is done, disable output and calculate averages
        if (cycleCounter >= cycles) {
            output = false;
            outputValue = minOutput;
            kp = pAverage / (cycleCounter - 1);
            ki = iAverage / (cycleCounter - 1);
            kd = dAverage / (cycleCounter - 1);
        }

        return outputValue;
    }





    void Reset() {
        cycleCounter = 0;
        output = true;
        outputValue = maxOutput;
        t1 = t2 = micros();
        tHigh = tLow = 0; 
        maxInput = -1000000;
        minInput = 1000000;
        pAverage = iAverage = dAverage = 0;
    }





    void setTargetInputValue(float target) {
        targetInputValue = target;
    }

    void setLoopInterval(long interval) {
        loopInterval = interval;
    }

    void setOutputRange(float minInput, float maxInput) {
        minOutput = minInput;
        maxOutput = maxInput;
    }

    void setZNMode(ZieglerNicholsMode zn) {
        znMode = zn;
    }

    void setTuningCycles(int tuneCycles) {
        cycles = tuneCycles;
    }



    float getKp() {
        return kp;
    };

    float getKi() {
        return ki;
    };

    float getKd() {
        return kd;
    };



    bool isFinished() {
        return (cycleCounter >= cycles);
    }
};
