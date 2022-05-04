#pragma once
#include "Core.h"
#include <initializer_list>




namespace Buttons {



class Event {
private:
	void (*onDoneHandle)() = nullptr;

public:
	struct ClickAction {
		class Click &click;
		class Action &action;
	};


public:
	Event(std::initializer_list<ClickAction> clickActions){};
	Event(ClickAction &clickAction){};

	void Enable();
	void Disable();

	void Tick();

	void SetOnDoneHandle(void (*_onDoneHandle)()) { onDoneHandle = _onDoneHandle; };
};




}
