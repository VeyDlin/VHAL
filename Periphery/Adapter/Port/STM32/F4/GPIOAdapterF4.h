#pragma once
#include <Adapter/GPIOAdapter.h>



using AGPIO = class GPIOAdapterF4;


class GPIOAdapterF4 : public GPIOAdapter<GPIO_TypeDef> {
public:
	using GPIOAdapter<GPIO_TypeDef>::operator=;

	GPIOAdapterF4() { }
	GPIOAdapterF4(GPIO_TypeDef *gpioPort, uint8 gpioPin, bool gpioInversion = false):GPIOAdapter(gpioPort, gpioPin, gpioInversion) { }
	GPIOAdapterF4(IO &io):GPIOAdapter(io) { }


	// Serves only the EXTI line of this pin, and only while the pin is in an interrupt mode. The line is cleared before
	// the user callback runs.
	virtual inline void IrqHandler() override {
		if(!LL_EXTI_IsActiveFlag_0_31(pin) || !IsExtiOwned()) {
			return;
		}

		LL_EXTI_ClearFlag_0_31(pin);

		if(!IsInterruptMode(parameters.mode)) {
			return;
		}

		GPIOAdapter<GPIO_TypeDef>::IrqHandler();
	}


	static inline ResultStatus AlternateInit(AlternateParameters val) {
		return GPIOAdapter<GPIO_TypeDef>::AlternateInitBase<AGPIO>(val);
	}

	static inline ResultStatus AlternateOpenDrainInit(AlternateParameters val)  {
		return GPIOAdapter<GPIO_TypeDef>::AlternateOpenDrainInitBase<AGPIO>(val);
	}

	static inline ResultStatus AnalogInit(AnalogParameters val)  {
		return GPIOAdapter<GPIO_TypeDef>::AnalogInitBase<AGPIO>(val);
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





	virtual ResultStatus Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}



		if(IsNormal() || IsAlternate() || IsAnalog()) {
			ReleaseExti();

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

			if(!IsExtiLineAvailable()) {
				return ResultStatus::notAvailable;
			}

			LL_GPIO_SetPinPull(port, pin, CastPull());
			LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_INPUT);

			ConfigureExti();

			InterruptInitialization();

			return AfterInitialization();
		}


		if(IsEvent()) {
			SystemAssert(eventPeripheryInit != nullptr);

			if(!IsExtiLineAvailable()) {
				return ResultStatus::notAvailable;
			}

			LL_GPIO_SetPinPull(port, pin, CastPull());
			LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_INPUT);

			ConfigureExti();

			EventInitialization();

			return AfterInitialization();
		}


		return ResultStatus::invalidParameter;
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
	static inline constexpr uint32 ExtiLines[16] = {
		LL_SYSCFG_EXTI_LINE0, LL_SYSCFG_EXTI_LINE1, LL_SYSCFG_EXTI_LINE2, LL_SYSCFG_EXTI_LINE3,
		LL_SYSCFG_EXTI_LINE4, LL_SYSCFG_EXTI_LINE5, LL_SYSCFG_EXTI_LINE6, LL_SYSCFG_EXTI_LINE7,
		LL_SYSCFG_EXTI_LINE8, LL_SYSCFG_EXTI_LINE9, LL_SYSCFG_EXTI_LINE10, LL_SYSCFG_EXTI_LINE11,
		LL_SYSCFG_EXTI_LINE12, LL_SYSCFG_EXTI_LINE13, LL_SYSCFG_EXTI_LINE14, LL_SYSCFG_EXTI_LINE15
	};



	// The line is in use when an interrupt, event or edge enable of it is set. Its owner is read from SYSCFG, which
	// ConfigureExti() has clocked, so IsExtiOwned() is called only for a line in use.
	bool IsExtiLineInUse() {
		return LL_EXTI_IsEnabledIT_0_31(pin) != 0 || LL_EXTI_IsEnabledEvent_0_31(pin) != 0
			|| LL_EXTI_IsEnabledRisingTrig_0_31(pin) != 0 || LL_EXTI_IsEnabledFallingTrig_0_31(pin) != 0;
	}



	bool IsExtiOwned() {
		return LL_SYSCFG_GetEXTISource(ExtiLines[PinIndex(pin)]) == CastExtiPort();
	}



	bool IsExtiLineAvailable() {
		bool lineInUse = IsExtiLineInUse();
		return CanTakeExtiLine(lineInUse, lineInUse && IsExtiOwned());
	}



	// Switches the EXTI line of this pin off when the pin owns it. Called when the pin leaves the interrupt and event
	// modes, so that its edges no longer raise the line.
	void ReleaseExti() {
		bool lineInUse = IsExtiLineInUse();
		if(!ShouldReleaseExtiLine(lineInUse, lineInUse && IsExtiOwned())) {
			return;
		}

		LL_EXTI_DisableIT_0_31(pin);
		LL_EXTI_DisableEvent_0_31(pin);
		LL_EXTI_DisableRisingTrig_0_31(pin);
		LL_EXTI_DisableFallingTrig_0_31(pin);
		LL_EXTI_ClearFlag_0_31(pin);
	}



	// Selects the port and the edges of the EXTI line, and sets the interrupt or the event mask. The SYSCFG clock is
	// enabled here, because the port selection lives in SYSCFG_EXTICR.
	void ConfigureExti() {
		LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
		LL_SYSCFG_SetEXTISource(CastExtiPort(), ExtiLines[PinIndex(pin)]);

		LL_EXTI_DisableIT_0_31(pin);
		LL_EXTI_DisableEvent_0_31(pin);
		LL_EXTI_ClearFlag_0_31(pin);

		if(HasRisingEdge(parameters.mode)) {
			LL_EXTI_EnableRisingTrig_0_31(pin);
		} else {
			LL_EXTI_DisableRisingTrig_0_31(pin);
		}

		if(HasFallingEdge(parameters.mode)) {
			LL_EXTI_EnableFallingTrig_0_31(pin);
		} else {
			LL_EXTI_DisableFallingTrig_0_31(pin);
		}

		if(IsInterruptMode(parameters.mode)) {
			LL_EXTI_EnableIT_0_31(pin);
		} else {
			LL_EXTI_EnableEvent_0_31(pin);
		}
	}



	uint32 CastExtiPort() const {
#ifdef GPIOA
		if(port == GPIOA) {
			return LL_SYSCFG_EXTI_PORTA;
		}
#endif

#ifdef GPIOB
		if(port == GPIOB) {
			return LL_SYSCFG_EXTI_PORTB;
		}
#endif

#ifdef GPIOC
		if(port == GPIOC) {
			return LL_SYSCFG_EXTI_PORTC;
		}
#endif

#ifdef GPIOD
		if(port == GPIOD) {
			return LL_SYSCFG_EXTI_PORTD;
		}
#endif

#ifdef GPIOE
		if(port == GPIOE) {
			return LL_SYSCFG_EXTI_PORTE;
		}
#endif

#ifdef GPIOF
		if(port == GPIOF) {
			return LL_SYSCFG_EXTI_PORTF;
		}
#endif

#ifdef GPIOG
		if(port == GPIOG) {
			return LL_SYSCFG_EXTI_PORTG;
		}
#endif

#ifdef GPIOH
		if(port == GPIOH) {
			return LL_SYSCFG_EXTI_PORTH;
		}
#endif

#ifdef GPIOI
		if(port == GPIOI) {
			return LL_SYSCFG_EXTI_PORTI;
		}
#endif

#ifdef GPIOJ
		if(port == GPIOJ) {
			return LL_SYSCFG_EXTI_PORTJ;
		}
#endif

#ifdef GPIOK
		if(port == GPIOK) {
			return LL_SYSCFG_EXTI_PORTK;
		}
#endif

		SystemAbort();
		return 0;
	}



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