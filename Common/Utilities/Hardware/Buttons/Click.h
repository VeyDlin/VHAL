#pragma once
#include "Core.h"
#include <initializer_list>




namespace Buttons {


class Click {
public:
	void (*onPushHandle)() = nullptr;
	void (*onPullHandle)() = nullptr;

public:
	Click(std::initializer_list<class Button> buttons){};
	Click(class Button &button){};
};




}
