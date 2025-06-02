#pragma once
#include "../../GPIOAdapter.h"



using AGPIO = class GPIOAdapterF4;


class GPIOAdapterF4 : public GPIOAdapter {
public:
	GPIOAdapterF4() { }
	GPIOAdapterF4(GPIO_TypeDef *gpioPort, uint8 gpioPin, bool gpioInversion = false):GPIOAdapter(gpioPort, gpioPin, gpioInversion) { }
	GPIOAdapterF4(IO &io):GPIOAdapter(io) { }


	static inline Status::statusType AlternateInit(AlternateParameters val) {
		return GPIOAdapter::AlternateInitBase<AGPIO>(val);
	}

	static inline Status::statusType AlternateOpenDrainInit(AlternateParameters val)  {
		return GPIOAdapter::AlternateOpenDrainInitBase<AGPIO>(val);
	}

	static inline Status::statusType AnalogInit(AnalogParameters val)  {
		return GPIOAdapter::AnalogInitBase<AGPIO>(val);
	}


protected:
	virtual inline bool GetPinState() override {
		if(parameters.mode == Mode::Output) {
			return static_cast<bool>(LL_GPIO_IsOutputPinSet(port, pin));
		}
		return static_cast<bool>(LL_GPIO_IsInputPinSet(port, pin));
	}





	virtual inline void SetPinState(bool state) override {
		if(state) {
			LL_GPIO_SetOutputPin(port, pin);
		} else {
			LL_GPIO_ResetOutputPin(port, pin);
		}
	}





	virtual inline void TogglePinState() override {
		LL_GPIO_TogglePin(port, pin);
	}





	virtual Status::statusType Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}



		if(IsNormal() || IsAlternate() || IsAnalog()) {
			LL_GPIO_InitTypeDef init = {
				.Pin = pin,
				.Mode = CastMode(),
				.Speed = CastSpeed(),
				.OutputType = CastOutputType(),
				.Pull = CastPull(),
				.Alternate = (uint32)(alternate)
			};
			SystemAssert(LL_GPIO_Init(port, &init) == ErrorStatus::SUCCESS);

			return AfterInitialization();
		}


		if(IsInterrupt()) {
			SystemAssert(interruptPeripheryInit != nullptr);

			LL_GPIO_SetPinPull(port, pin, CastPull());
			LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_INPUT);

			// TODO: Add LL_EXTI_Init

			InterruptInitialization();

			return AfterInitialization();
		}


		if(IsEvent()) {
			SystemAssert(eventPeripheryInit != nullptr);

			LL_GPIO_SetPinPull(port, pin, CastPull());
			LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_INPUT);

			EventInitialization();

			return AfterInitialization();
		}


		return Status::invalidParameter;
	}





	virtual void OnEnableClock() {
#ifdef GPIOA
			if(port == GPIOA) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
				return;
			}
#endif

#ifdef GPIOB
			if(port == GPIOB) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
				return;
			}
#endif

#ifdef GPIOC
			if(port == GPIOC) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
				return;
			}
#endif

#ifdef GPIOD
			if(port == GPIOD) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);
				return;
			}
#endif

#ifdef GPIOE
			if(port == GPIOE) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOE);
				return;
			}
#endif

#ifdef GPIOF
			if(port == GPIOF) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOF);
				return;
			}
#endif

#ifdef GPIOG
			if(port == GPIOG) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOG);
				return;
			}
#endif

#ifdef GPIOH
			if(port == GPIOH) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOH);
				return;
			}
#endif

#ifdef GPIOI
			if(port == GPIOI) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOI);
				return;
			}
#endif

#ifdef GPIOJ
			if(port == GPIOJ) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOJ);
				return;
			}
#endif

#ifdef GPIOK
			if(port == GPIOK) {
				LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOK);
				return;
			}
#endif
	}





private:
	constexpr uint32 CastMode() const {
		switch (parameters.mode) {
			case Mode::Output:
			case Mode::OpenDrain:
				return LL_GPIO_MODE_OUTPUT;
			break;

			case Mode::Alternate:
				return LL_GPIO_MODE_ALTERNATE;
			break;

			case Mode::Analog:
				return LL_GPIO_MODE_ANALOG;
			break;

			case Mode::Input:
			default:
				return LL_GPIO_MODE_INPUT;
			break;
		}
	}



	constexpr uint32 CastSpeed() const {
		switch (parameters.speed) {
			case Speed::Low:
				return LL_GPIO_SPEED_FREQ_LOW;
			break;

			case Speed::Medium:
				return LL_GPIO_SPEED_FREQ_MEDIUM;
			break;

			case Speed::High:
				return LL_GPIO_SPEED_FREQ_HIGH;
			break;

			case Speed::VeryHigh:
				return LL_GPIO_SPEED_FREQ_VERY_HIGH;
			break;

			default:
				return LL_GPIO_SPEED_FREQ_MEDIUM;
			break;
		}
	}



	constexpr uint32 CastPull() const {
		switch (parameters.pull) {
			case Pull::None:
				return LL_GPIO_PULL_NO;
			break;

			case Pull::Up:
				return LL_GPIO_PULL_UP;
			break;

			case Pull::Down:
				return LL_GPIO_PULL_DOWN;
			break;

			default:
				return LL_GPIO_PULL_NO;
			break;
		}
	}



	constexpr uint32 CastOutputType() const {
		switch (parameters.mode) {
			case Mode::OpenDrain:
			case Mode::AlternateOpenDrain:
				return LL_GPIO_OUTPUT_OPENDRAIN;
			break;

			case Mode::Output:
			default:
				return LL_GPIO_OUTPUT_PUSHPULL;
			break;
		}
	}



	inline bool IsNormal() {
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















