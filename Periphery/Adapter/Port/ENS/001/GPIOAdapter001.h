#pragma once
#include <Adapter/GPIOAdapter.h>


using AGPIO = class GPIOAdapter001;


class GPIOAdapter001 : public GPIOAdapter<CMSDK_GPIO_TypeDef> {
public:
	using GPIOAdapter<CMSDK_GPIO_TypeDef>::operator=;

	GPIOAdapter001() { }
	GPIOAdapter001(CMSDK_GPIO_TypeDef *gpioPort, uint8 gpioPin, bool gpioInversion = false):GPIOAdapter(gpioPort, gpioPin, gpioInversion) { }


	static inline ResultStatus AlternateInit(AlternateParameters val) {
		return GPIOAdapter<CMSDK_GPIO_TypeDef>::AlternateInitBase<AGPIO>(val);
	}

	static inline ResultStatus AlternateOpenDrainInit(AlternateParameters val)  {
		return GPIOAdapter<CMSDK_GPIO_TypeDef>::AlternateOpenDrainInitBase<AGPIO>(val);
	}

	static inline ResultStatus AnalogInit(AnalogParameters val)  {
		return GPIOAdapter<CMSDK_GPIO_TypeDef>::AnalogInitBase<AGPIO>(val);
	}


protected:
	virtual inline bool GetPinState() override {
		if(parameters.mode == Mode::Output) {
			return static_cast<bool>(port->DATAOUT & pin);
		}
		return static_cast<bool>(port->DATAIN & pin);
	}





	virtual inline void SetPinState(bool state) override {
		if(state) {
			port->BITSET = pin;
		} else {
			port->BITCLR = pin;
		}
	}





	virtual inline void TogglePinState() override {
		port->DATAOUT ^= pin;
	}





	virtual ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}



		if(IsNormal()) {
			switch(parameters.mode) {
				case Mode::Input:
					port->OE &= ~pin;
					port->IE |= pin;
					port->ODEN &= ~pin;
				break;

				case Mode::Output:
					port->OE |= pin;
					port->IE &= ~pin;
					port->ODEN &= ~pin;
				break;

				case Mode::OpenDrain:
					port->OE |= pin;
					port->IE &= ~pin;
					port->ODEN |= pin;
				break;

				default:
				break;
			}

			CastPull();
			return AfterInitialization();
		}


		if(IsAlternate()) {
			// Determine pin number from bitmask
			uint8 pinNumber = 0;
			uint32 tmp = pin;
			while(tmp >>= 1) {
				pinNumber++;
			}

			// ALTFL covers pins 0-15 (2 bits per pin)
			// ALTFH covers pins 16-23 (2 bits per pin)
			if(pinNumber < 16) {
				uint32 shift = pinNumber * 2;
				port->ALTFL &= ~(0x3 << shift);
				port->ALTFL |= ((uint32)alternate & 0x3) << shift;
			} else {
				uint32 shift = (pinNumber - 16) * 2;
				port->ALTFH &= ~(0x3 << shift);
				port->ALTFH |= ((uint32)alternate & 0x3) << shift;
			}

			if(parameters.mode == Mode::AlternateOpenDrain) {
				port->ODEN |= pin;
			} else {
				port->ODEN &= ~pin;
			}

			CastPull();
			return AfterInitialization();
		}


		if(IsAnalog()) {
			// Datasheet requires ALTF=0b11 AND ANAEN=1 for analog mode
			uint8 pinNumber = 0;
			uint32 tmp = pin;
			while(tmp >>= 1) {
				pinNumber++;
			}

			if(pinNumber < 16) {
				uint32 shift = pinNumber * 2;
				port->ALTFL &= ~(0x3 << shift);
				port->ALTFL |= (0x3 << shift);
			} else {
				uint32 shift = (pinNumber - 16) * 2;
				port->ALTFH &= ~(0x3 << shift);
				port->ALTFH |= (0x3 << shift);
			}

			port->OE &= ~pin;
			port->IE &= ~pin;
			port->ANAEN |= pin;

			CastPull();
			return AfterInitialization();
		}


		if(IsInterrupt()) {
			SystemAssert(interruptPeripheryInit != nullptr);

			port->OE &= ~pin;
			port->IE |= pin;
			CastPull();

			InterruptInitialization();

			return AfterInitialization();
		}


		if(IsEvent()) {
			SystemAssert(eventPeripheryInit != nullptr);

			port->OE &= ~pin;
			port->IE |= pin;
			CastPull();

			EventInitialization();

			return AfterInitialization();
		}


		return ResultStatus::invalidParameter;
	}





	virtual ResultStatus InterruptInitialization() override {
		// Configure EXTI edge detection based on mode
		switch(parameters.mode) {
			case Mode::InterruptRising:
				CMSDK_EXTI->RTSR |= pin;
				CMSDK_EXTI->FTSR &= ~pin;
			break;

			case Mode::InterruptFalling:
				CMSDK_EXTI->RTSR &= ~pin;
				CMSDK_EXTI->FTSR |= pin;
			break;

			case Mode::InterruptRisingFalling:
				CMSDK_EXTI->RTSR |= pin;
				CMSDK_EXTI->FTSR |= pin;
			break;

			default:
			break;
		}

		// Enable interrupt mask
		CMSDK_EXTI->IMR |= pin;

		if(interruptPeripheryInit != nullptr) {
			return interruptPeripheryInit();
		}

		return ResultStatus::ok;
	}





	virtual ResultStatus EventInitialization() override {
		// Configure EXTI edge detection based on mode
		switch(parameters.mode) {
			case Mode::EventRising:
				CMSDK_EXTI->RTSR |= pin;
				CMSDK_EXTI->FTSR &= ~pin;
			break;

			case Mode::EventFalling:
				CMSDK_EXTI->RTSR &= ~pin;
				CMSDK_EXTI->FTSR |= pin;
			break;

			case Mode::EventRisingFalling:
				CMSDK_EXTI->RTSR |= pin;
				CMSDK_EXTI->FTSR |= pin;
			break;

			default:
			break;
		}

		// Enable event mask
		CMSDK_EXTI->EMR |= pin;

		if(eventPeripheryInit != nullptr) {
			return eventPeripheryInit();
		}

		return ResultStatus::ok;
	}





private:
	inline void CastPull() {
		switch (parameters.pull) {
			case Pull::Up:
				port->PU |= pin;
				port->PD &= ~pin;
			break;

			case Pull::Down:
				port->PU &= ~pin;
				port->PD |= pin;
			break;

			case Pull::None:
			default:
				port->PU &= ~pin;
				port->PD &= ~pin;
			break;
		}
	}



	constexpr bool IsNormal() const {
		switch (parameters.mode) {
			case Mode::Input:
			case Mode::Output:
			case Mode::OpenDrain:
				return true;
			break;

			default:
				return false;
			break;
		}
	}


	inline bool IsInterrupt() {
		switch (parameters.mode) {
			case Mode::InterruptRising:
			case Mode::InterruptFalling:
			case Mode::InterruptRisingFalling:
				return true;
			break;

			default:
				return false;
			break;
		}
	}



	inline bool IsEvent() {
		switch (parameters.mode) {
			case Mode::EventRising:
			case Mode::EventFalling:
			case Mode::EventRisingFalling:
				return true;
			break;

			default:
				return false;
			break;
		}
	}



	inline bool IsAlternate() {
		switch (parameters.mode) {
			case Mode::Alternate:
			case Mode::AlternateOpenDrain:
				return true;
			break;

			default:
				return false;
			break;
		}
	}



	inline bool IsAnalog() {
		return parameters.mode == Mode::Analog;
	}
};
