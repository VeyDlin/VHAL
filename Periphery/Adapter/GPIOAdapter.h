#pragma once
#include "IAdapter.h"
#include <type_traits>

#define VHAL_GPIO_ADAPTER


template<typename PortType = void>
class GPIOAdapter: public IAdapter {
public:
	static constexpr bool hasPort = !std::is_void_v<PortType>;

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
		PortType *port;
		uint8 pin;
		uint8 alternate = 0;
		Pull pull = Pull::None;
		Speed speed = Speed::Low;
	};

	struct AnalogParameters {
		PortType *port;
		uint8 pin;
		Pull pull = Pull::None;
	};


	struct IO {
		PortType *port;
		uint8 pin;
	};


protected:
	PortType *port;
	uint32 pin;
	bool inversion;
	Parameters parameters;
	uint8 alternate = 0;


public:
	std::function<void(bool state)> onInterrupt;
	std::function<ResultStatus()> interruptPeripheryInit;
	std::function<ResultStatus()> eventPeripheryInit;



	GPIOAdapter() = default;

	GPIOAdapter(PortType *gpioPort, uint8 gpioPin, bool gpioInversion = false) 
		: port(gpioPort), pin(hasPort ? (1u << gpioPin) : gpioPin), inversion(gpioInversion) { }

	GPIOAdapter(IO &io) 
		: port(io.port), pin(hasPort ? (1u << io.pin) : io.pin), inversion(false) { }

	GPIOAdapter(uint8 gpioPin, bool gpioInversion = false) requires (!hasPort) 
		: port(nullptr), pin(gpioPin), inversion(gpioInversion) { }



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

	inline uint8 GetByteState(uint8 offset = 0) {
		bool state = GetPinState();
		state = inversion ? !state : state;
		return static_cast<uint8>(state) << offset;
	}

	inline GPIOAdapter& Toggle() {
		TogglePinState();
		return *this;
	}





	inline GPIOAdapter& Wait(bool state) {
		while(GetState() != state);
		return *this;
	}

	inline uint32 GetPin() {
		return pin;
	}
	PortType* GetPort() requires (hasPort) {
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





	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}





	virtual inline GPIOAdapter& SetAlternate(uint8 val) {
		alternate = val;
		return *this;
	}





	template<typename AdapterClass>
	static inline ResultStatus AlternateInitBase(AlternateParameters val) {
		AdapterClass io = { val.port, val.pin };
		io.SetAlternate(val.alternate);
		return io.SetParameters({ Mode::Alternate, val.pull, val.speed });
	}





	template<typename AdapterClass>
	static inline ResultStatus AlternateOpenDrainInitBase(AlternateParameters val) {
		AdapterClass io = { val.port, val.pin };
		io.SetAlternate(val.alternate);
		return io.SetParameters({ Mode::AlternateOpenDrain, val.pull, val.speed });
	}





	template<typename AdapterClass>
	static inline ResultStatus AnalogInitBase(AnalogParameters val) {
		AdapterClass io = { val.port, val.pin };
		return io.SetParameters({ Mode::Analog, val.pull });
	}



protected:
	virtual ResultStatus Initialization() = 0;


	virtual bool GetPinState() = 0;
	virtual void SetPinState(bool state) = 0;
	virtual void TogglePinState() = 0;


	inline virtual ResultStatus InterruptInitialization() {
		if(interruptPeripheryInit != nullptr) {
			return interruptPeripheryInit();
		}

		return ResultStatus::ok;
	}



	inline virtual ResultStatus EventInitialization() {
		if(eventPeripheryInit != nullptr) {
			return eventPeripheryInit();
		}

		return ResultStatus::ok;
	}
};















