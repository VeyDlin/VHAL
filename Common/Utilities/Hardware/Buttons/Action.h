#pragma once
#include <VHAL.h>
#include <chrono>

namespace Buttons {


class Action {
public:
	enum class ClickType {
		Short,
		Long,
		VeryLong,
		// Hold until released: the action completes on release, for any hold length.
		// Behaves like Other. The sequence does not time out while the click is held.
		Infinitely, // Hold until released
		Other
	};

private:
	ClickType clickType = ClickType::Short;
	uint8 clicksCount = 1;

public:
	Action() = default;

	// A count of 0 cannot be reported from a constructor. IsValid() is false for it,
	// and Event and Core reject such an action.
	Action(ClickType type, uint8 count)
		: clickType(type), clicksCount(count) { }

	Action& SetClickType(ClickType type) {
		clickType = type;
		return *this;
	}

	// Returns false and leaves the count unchanged when count is 0.
	bool SetClicksCount(uint8 count) {
		if (count == 0) {
			return false;
		}
		clicksCount = count;
		return true;
	}

	Action& Get() { return *this; }

	ClickType GetClickType() const { return clickType; }

	uint8 GetClicksCount() const { return clicksCount; }

	bool IsValid() const { return clicksCount > 0; }

	bool IsValidDuration(std::chrono::milliseconds duration,
		std::chrono::milliseconds shortThreshold,
		std::chrono::milliseconds longThreshold) const
	{
		switch (clickType) {
			case ClickType::Short: return duration < shortThreshold;
			case ClickType::Long: return duration >= shortThreshold && duration < longThreshold;
			case ClickType::VeryLong: return duration >= longThreshold;
			case ClickType::Infinitely: return true;
			default: return true;
		}
	}
};

}
