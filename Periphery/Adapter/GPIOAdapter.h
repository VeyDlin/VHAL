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



	// Replaces onInterrupt while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnInterrupt(std::function<void(bool state)> handler) {
		std::function<void(bool state)> old;
		System::CriticalSection(true);
		old = std::move(onInterrupt);
		onInterrupt = std::move(handler);
		System::CriticalSection(false);
	}


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





	// Called from the EXTI vector of the pin. The STM32 ports override it: they call this only when their own line is
	// pending, and clear the pending bit first.
	virtual inline void IrqHandler() {
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
	// Index of the lowest bit of the pin mask: 0 to 15 for a pin of a port, and the EXTI line of the pin. A zero mask
	// gives 31.
	static constexpr uint8 PinIndex(uint32 pinMask) {
		uint8 index = 0;
		while(index < 31 && (pinMask & (1u << index)) == 0) {
			index++;
		}

		return index;
	}


	static constexpr bool IsInterruptMode(Mode value) {
		return value == Mode::InterruptRising || value == Mode::InterruptFalling || value == Mode::InterruptRisingFalling;
	}


	static constexpr bool IsEventMode(Mode value) {
		return value == Mode::EventRising || value == Mode::EventFalling || value == Mode::EventRisingFalling;
	}


	// The rising edge that the mode selects for the EXTI line.
	static constexpr bool HasRisingEdge(Mode value) {
		return value == Mode::InterruptRising || value == Mode::InterruptRisingFalling
			|| value == Mode::EventRising || value == Mode::EventRisingFalling;
	}


	// The falling edge that the mode selects for the EXTI line.
	static constexpr bool HasFallingEdge(Mode value) {
		return value == Mode::InterruptFalling || value == Mode::InterruptRisingFalling
			|| value == Mode::EventFalling || value == Mode::EventRisingFalling;
	}


	// A pin may set up its EXTI line when no other port uses the line, or when this port already owns it.
	static constexpr bool CanTakeExtiLine(bool lineInUse, bool lineOwned) {
		return !lineInUse || lineOwned;
	}


	// A pin switches its EXTI line off when it leaves the interrupt or event modes, and only when it owns the line.
	static constexpr bool ShouldReleaseExtiLine(bool lineInUse, bool lineOwned) {
		return lineInUse && lineOwned;
	}


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















