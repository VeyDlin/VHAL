#pragma once
#include "IAdapter.h"

#define AUSED_GPIO_ADAPTER



class GPIOAdapter: public IAdapter {
public:
	enum class Pull { Up, Down, None };
	enum class Speed { Low, Medium, High, VeryHigh };
	enum class Mode {
		Input, Output, OpenDrain,
		InterruptRising, InterruptFalling, InterruptRisingFalling,
		EventRising, EventFalling, EventRisingFalling,
		Alternate, AlternateOpenDrain, Analog
	};

	enum class AlternateMode {
		InterruptRising, InterruptFalling, InterruptRisingFalling,
		EventRising, EventFalling, EventRisingFalling,
		Alternate, Analog
	};


	struct Parameters {
		Mode mode = Mode::Input;
		Pull pull = Pull::None;
		Speed speed = Speed::Low;
	};

	struct AlternateParameters {
		GPIO_TypeDef *port;
		uint8 pin;
		uint8 alternate = 0;
		Pull pull = Pull::None;
		Speed speed = Speed::Low;
	};

	struct AnalogParameters {
		GPIO_TypeDef *port;
		uint8 pin;
		Pull pull = Pull::None;
	};


	struct IO {
		GPIO_TypeDef *port;
		uint8 pin;
	};


protected:
	GPIO_TypeDef *port;
	uint16 pin;
	bool inversion;
	Parameters parameters;
	uint8 alternate = 0;


public:
	std::function<void(bool state)> onInterrupt;

	std::function<Status::statusType()> interruptPeripheryInit;
	std::function<Status::statusType()> eventPeripheryInit;



	GPIOAdapter() { }

	GPIOAdapter(GPIO_TypeDef *gpioPort, uint8 gpioPin, bool gpioInversion = false) : port(gpioPort), pin(1 << gpioPin), inversion(gpioInversion) { }

	GPIOAdapter(IO &io) : port(io.port), pin(1 << io.pin), inversion(false) { }



	inline GPIOAdapter& SetState(bool state) {
		SetPinState(inversion ? !state : state);
		return *this;
	}

	inline GPIOAdapter& Set() {
		SetPinState(inversion ? false : true);
		return *this;
	}

	inline GPIOAdapter& Reset() {
		SetPinState(inversion ? true : false);
		return *this;
	}

	inline bool GetState() {
		bool state = GetPinState();
		return inversion ? !state : state;
	}

	inline GPIOAdapter& Toggle() {
		TogglePinState();
		return *this;
	}





	inline GPIOAdapter& Wait(bool state) {
		while(GetState() != state);
		return *this;
	}

	inline uint16 GetPin() {
		return pin;
	}
	GPIO_TypeDef* GetPort() {
		return port;
	}
	inline bool IsInversion() {
		return inversion;
	}
	inline void SetInversion(bool val) {
		inversion = val;
	}





	inline void operator= (bool state) {
		SetState(state);
    }





	inline void IrqHandler() {
		if(onInterrupt != nullptr) {
			onInterrupt(GetState());
		}
	}





	virtual Status::statusType SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}





	virtual inline GPIOAdapter& SetAlternate(uint8 val) {
		alternate = val;
		return *this;
	}





	template<typename AdapterClass>
	static inline Status::statusType AlternateInit(AlternateParameters val) {
		AdapterClass io = { val.port, val.pin };
		io.SetAlternate(val.alternate);
		return io.SetParameters({ Mode::Alternate, val.pull, val.speed });
	}





	template<typename AdapterClass>
	static inline Status::statusType AlternateOpenDrainInit(AlternateParameters val) {
		AdapterClass io = { val.port, val.pin };
		io.SetAlternate(val.alternate);
		return io.SetParameters({ Mode::AlternateOpenDrain, val.pull, val.speed });
	}





	template<typename AdapterClass>
	static inline Status::statusType AnalogInit(AnalogParameters val) {
		AdapterClass io = { val.port, val.pin };
		return io.SetParameters({ Mode::Analog, val.pull });
	}



protected:
	virtual Status::statusType Initialization() = 0;


	virtual bool GetPinState() = 0;
	virtual void SetPinState(bool state) = 0;
	virtual void TogglePinState() = 0;


	inline virtual Status::statusType InterruptInitialization() {
		if(interruptPeripheryInit != nullptr) {
			return interruptPeripheryInit();
		}

		return Status::ok;
	}



	inline virtual Status::statusType EventInitialization() {
		if(eventPeripheryInit != nullptr) {
			return eventPeripheryInit();
		}

		return Status::ok;
	}
};















