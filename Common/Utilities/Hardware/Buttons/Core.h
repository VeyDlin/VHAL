#pragma once
#include <BSP.h>

#include "Action.h"
#include "Button.h"
#include "Click.h"
#include "Event.h"



	

/*
	Buttons::Button keyA = {GPIOB, GPIO_PIN_0};
	Buttons::Button keyB = {GPIOB, GPIO_PIN_1};
	Buttons::Button keyC = {GPIOB, GPIO_PIN_2};
	Buttons::Button keyD = {GPIOB, GPIO_PIN_3};

	Buttons::Click click1 = {keyA, keyB, keyC};
	Buttons::Action action1 = {Buttons::Action::Long, 4}; // 4 РґРѕР»РіРёС… РЅР°Р¶Р°С‚РёСЏ

	Buttons::Click click2 = keyD;
	auto action2 = Buttons::Action().ClickType(Buttons::Action::Short).ClicksCount(1); // 1 РєРѕСЂРѕС‚РєРѕРµ РЅР°Р¶Р°С‚РёРµ

	Buttons::Event startTest = {
		{click1, action1},
		{click2, action2},
		{click2, (Buttons::Action{Buttons::Action::Long, 1}).Get()} // 1 РґРѕР»РіРѕРµ РЅР°Р¶Р°С‚РёРµ
	};

	startTest.SetOnDoneHandle([]() {
	     // РџСЂРѕРёР·РѕС€Р»Рѕ 4 РґРѕР»РіРёС… РѕРґРЅРѕРІСЂРµРјРµРЅРЅС‹С… РЅР°Р¶Р°С‚РёСЏ РЅР° РєРЅРѕРїРєРё 1, 2, 3
	     // Рђ РїРѕСЃР»Рµ РѕРґРЅРѕ РєРѕСЂРѕС‚РєРѕРµ Рё РѕРґРЅРѕ РґРѕР»РіРѕРµ РЅР° РєРЅРѕРїРєСѓ 6
	});
*/


namespace Buttons {




}
