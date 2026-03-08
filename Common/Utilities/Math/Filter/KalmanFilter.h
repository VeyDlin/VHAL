#pragma once
#include <VHAL.h>
#include <Utilities/Math/IQMath/IQ.h>


template <RealType Type = float>
class KalmanFilter {
private:
    Type processNoise; 			// Process noise covariance (R)
    Type measurementNoise; 		// Measurement noise covariance (Q)
    Type stateTransition; 		// State transition coefficient (A)
    Type controlInputEffect; 	// Control input coefficient (B)
    Type measurementMapping; 	// Measurement mapping coefficient (C)
    Type covariance; 			// Error covariance
    Type stateEstimate; 		// Current estimated state (filtered signal)

    bool isInitialized = false; // Indicates whether the filter has been initialized

public:
    KalmanFilter(
		Type processNoise, Type measurementNoise, Type stateTransition, Type controlInputEffect, Type measurementMapping
	):
		processNoise(processNoise),
		measurementNoise(measurementNoise),
        stateTransition(stateTransition),
		controlInputEffect(controlInputEffect),
        measurementMapping(measurementMapping)
	{
        covariance = (Type(1) / measurementMapping) * measurementNoise * (Type(1) / measurementMapping);
    }


    // Filters a new measurement
    // z - The latest measurement to be filtered
    // u - Control input
    Type Filter(Type measurement, Type controlInput) {
        // Initialize the filter state on the first call
        if (!isInitialized) {
            isInitialized = true;
            stateEstimate = (Type(1) / measurementMapping) * measurement;
            return stateEstimate;
        }

        // Prediction step
        Type predictedState = (stateTransition * stateEstimate) + (controlInputEffect * controlInput);
        Type predictedCovariance = ((stateTransition * covariance) * stateTransition) + processNoise;

        // Calculate Kalman gain
        Type kalmanGain = predictedCovariance * measurementMapping *
                           (Type(1) / ((measurementMapping * predictedCovariance * measurementMapping) + measurementNoise));

        // Correction step
        stateEstimate = predictedState + kalmanGain * (measurement - (measurementMapping * predictedState));
        covariance = predictedCovariance - (kalmanGain * measurementMapping * predictedCovariance);

        return stateEstimate;
    }


    // Returns the last filtered state estimate
    Type GetLastStateEstimate() const { return stateEstimate; }

    // Sets the measurement noise covariance (Q)
    void SetMeasurementNoise(Type noise) { measurementNoise = noise; }

    // Sets the process noise covariance (R)
    void SetProcessNoise(Type noise) { processNoise = noise; }
};