#pragma once

template <typename intType = float>
class IntWindow {
public:
	enum class PullType {
		noPull,  // halfGap Рє РїРѕР»РѕР¶РёС‚РµР»СЊРЅРѕРјСѓ Рё РѕС‚СЂРёС†Р°С‚РµР»СЊРЅРѕРјСѓ
		pullUp,  // halfGap С‚РѕР»СЊРєРѕ Рє РїРѕР»РѕР¶РёС‚РµР»СЊРЅРѕРјСѓ, РµСЃР»Рё Р·РЅР°С‡РµРЅРёРµ С‡СѓС‚СЊ СѓРјРµРЅСЊС€РµС‚СЃСЏ С‚Рѕ lastValue С‚СѓС‚ Р¶Рµ РѕР±РЅРѕРІРёС‚СЃСЏ
		pullDown // halfGap С‚РѕР»СЊРєРѕ Рє РѕС‚СЂРёС†Р°С‚РµР»СЊРЅРѕРјСѓ, РµСЃР»Рё Р·РЅР°С‡РµРЅРёРµ С‡СѓС‚СЊ СѓРІРµР»РёС‡РёС‚СЃСЏ С‚Рѕ lastValue С‚СѓС‚ Р¶Рµ РѕР±РЅРѕРІРёС‚СЃСЏ
	};

private:
	intType lastValue = 0;
	intType lastGap[2] = { 0, 0 };
	intType halfGap;
	PullType pull;


public:
	IntWindow() {}
	IntWindow(intType setHalfGap, PullType setPull = PullType::noPull) : halfGap(setHalfGap), pull(setPull) {}

	intType Get(intType value) {
		if (lastGap[0] == lastGap[1]) {
			lastGap[0] = value - halfGap;
			lastGap[1] = value + halfGap;
			lastValue = value;
		}

		if (value > lastGap[1] || (pull == PullType::pullUp && value > lastValue)) {
			lastGap[1] = value;
			lastGap[0] = value - halfGap * 2;
			lastValue = value;
		} else if (value < lastGap[0] || (pull == PullType::pullDown && value < lastValue)) {
			lastGap[0] = value;
			lastGap[1] = value + halfGap * 2;
			lastValue = value;
		}

		return lastValue;
	}
};
