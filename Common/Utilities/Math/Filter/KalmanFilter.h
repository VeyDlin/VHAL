#pragma once
#include <BSP.h>

/*
    Example usage:
    
    KalmanFilter kalmanFilter(0.01, 3.0, 1.0, 0.0, 1.0);

    float filteredValue = kalmanFilter.Filter(getSensorValue(), 0.0);
    Console::LogLn(filteredValue);
*/

class KalmanFilter {
private:
    float processNoise; 		// Process noise covariance (R)
    float measurementNoise; 	// Measurement noise covariance (Q)
    float stateTransition; 		// State transition coefficient (A)
    float controlInputEffect; 	// Control input coefficient (B)
    float measurementMapping; 	// Measurement mapping coefficient (C)
    float covariance; 			// Error covariance
    float stateEstimate; 		// Current estimated state (filtered signal)

    bool isInitialized = false; // Indicates whether the filter has been initialized

public:
    KalmanFilter(
		float processNoise, float measurementNoise, float stateTransition, float controlInputEffect, float measurementMapping
	): 
		processNoise(processNoise),
		measurementNoise(measurementNoise), 
        stateTransition(stateTransition), 
		controlInputEffect(controlInputEffect), 
        measurementMapping(measurementMapping) 
	{
        covariance = (1 / measurementMapping) * measurementNoise * (1 / measurementMapping);
    }


    // Filters a new measurement
    // z - The latest measurement to be filtered
    // u - Control input
    float Filter(float measurement, float controlInput) {
        // Initialize the filter state on the first call
        if (!isInitialized) {
            isInitialized = true;
            stateEstimate = (1 / measurementMapping) * measurement;
            return stateEstimate;
        }

        // Prediction step
        float predictedState = (stateTransition * stateEstimate) + (controlInputEffect * controlInput);
        float predictedCovariance = ((stateTransition * covariance) * stateTransition) + processNoise;

        // Calculate Kalman gain
        float kalmanGain = predictedCovariance * measurementMapping * 
                           (1 / ((measurementMapping * predictedCovariance * measurementMapping) + measurementNoise));

        // Correction step
        stateEstimate = predictedState + kalmanGain * (measurement - (measurementMapping * predictedState));
        covariance = predictedCovariance - (kalmanGain * measurementMapping * predictedCovariance);

        return stateEstimate;
    }


    // Returns the last filtered state estimate
    float GetLastStateEstimate() const { return stateEstimate; }

    // Sets the measurement noise covariance (Q)
    void SetMeasurementNoise(float noise) { measurementNoise = noise; }

    // Sets the process noise covariance (R)
    void SetProcessNoise(float noise) { processNoise = noise; }
};