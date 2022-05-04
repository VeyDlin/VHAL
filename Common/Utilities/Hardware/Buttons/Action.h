#pragma once
#include "Core.h"



namespace Buttons {




class Action {
public:
	enum clickType {
		Short,
		Long,
		VeryLong,
		Infinitely,
		Other
	};

public:
	Action() { };
	Action(clickType _clickType, uint8 clicksCount) { };

	Action& ClickType(clickType _clickType) { return *this; }
	Action& ClicksCount(uint8 clicksCount) { return *this; }
	Action& Get() { return *this; }
};




}
