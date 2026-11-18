#pragma once
#include <VHAL.h>
#include <vector>
#include <Utilities/Math/Filter/Hysteresis.h>
#include <Utilities/Math/MathUtilities.h>
#include <Utilities/Math/IQMath/IQ.h>


template <RealType Type = float>
class BatteryLevel {
public:
	// Represents a point in the voltage-to-level profile
	struct BatteryProfilePoint {
		Type voltage;	// voltage The voltage of the battery
		uint8 level;	// level The corresponding charge level (0-100%)
	};


private:
    Hysteresis<Type> voltageLevel;  // Filters battery voltage to reduce noise
    uint8 cells;                     // Number of cells in the battery pack
    uint16 divisions;                // Number of charge level divisions
    uint16 profileCount;             // Number of points in the battery profile
    std::vector<BatteryProfilePoint> profile;  // Battery profile points, owned by the object


public:
	BatteryLevel(const std::initializer_list<BatteryProfilePoint>& batteryProfile, uint8 divisionsQuantity = 10, uint8 cellsQuantity = 1, Type jitter = Type(0)) {
		cells = cellsQuantity;
		divisions = divisionsQuantity;

		profile.assign(batteryProfile.begin(), batteryProfile.end());
		profileCount = static_cast<uint16>(profile.size());

		// Without a profile there is no voltage range to derive the jitter from
		if (jitter <= Type(0) && profileCount > 0) {
			Type maxVoltage = profile[0].voltage * Type(static_cast<int>(cells));
			Type minVoltage = profile[profileCount - 1].voltage * Type(static_cast<int>(cells));
			jitter = ((maxVoltage - minVoltage) / Type(static_cast<int>(divisions))) / Type(2);
		}

		voltageLevel = Hysteresis<Type>(jitter, Hysteresis<Type>::PullType::NoPull);
	}


	uint8 GetDivision(Type batteryVoltage) {
		Type voltage = voltageLevel.Get(batteryVoltage);
		voltage /= Type(static_cast<int>(cells));

		// An empty profile has no curve to read; the level is reported as 0
		if (profileCount == 0) {
			return 0;
		}

		if (voltage >= profile[0].voltage) {
			return divisions;
		}

		if (voltage <= profile[profileCount - 1].voltage) {
			return 0;
		}

		Type level = Type(0);
		for (int32 i = profileCount - 1; i >= 0; i--) {
			if (voltage < profile[i].voltage) {
				level = MathUtilities::Extrapolation<Type>(
					profile[i].voltage, Type(static_cast<int>(profile[i].level)),
					profile[i + 1].voltage, Type(static_cast<int>(profile[i + 1].level)),
					voltage
				);
				break;
			}
		}

		return static_cast<uint8>(static_cast<int32>(level / (Type(100) / Type(static_cast<int>(divisions)))));
	}
};
