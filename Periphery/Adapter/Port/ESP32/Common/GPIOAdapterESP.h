#pragma once
#include <Adapter/GPIOAdapter.h>


using AGPIO = class GPIOAdapterESP;


class GPIOAdapterESP : public GPIOAdapter<> {
protected:
	bool outputState = false;


private:
	static inline bool isrServiceInstalled = false;
	bool interruptAttached = false;


public:
	using GPIOAdapter<>::operator=;

	GPIOAdapterESP() = default;
	GPIOAdapterESP(uint8 gpioPin, bool gpioInversion = false) : GPIOAdapter(gpioPin, gpioInversion) { }

	GPIOAdapterESP(const GPIOAdapterESP &other) : GPIOAdapter(other), outputState(other.outputState) { }

	GPIOAdapterESP& operator=(const GPIOAdapterESP &other) {
		if (this == &other) {
			return *this;
		}

		DetachInterrupt();
		GPIOAdapter::operator=(other);
		outputState = other.outputState;
		return *this;
	}

	~GPIOAdapterESP() override {
		DetachInterrupt();
	}


protected:
	virtual inline bool GetPinState() override {
		return static_cast<bool>(gpio_get_level(static_cast<gpio_num_t>(pin)));
	}


	virtual inline void SetPinState(bool state) override {
		outputState = state;
		gpio_set_level(static_cast<gpio_num_t>(pin), state ? 1 : 0);
	}


	virtual inline void TogglePinState() override {
		SetPinState(!outputState);
	}


	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		gpio_config_t io_conf = {};
		io_conf.pin_bit_mask = (1ULL << pin);
		io_conf.intr_type = GPIO_INTR_DISABLE;
		bool isInterruptMode = false;

		switch (parameters.mode) {
			case Mode::Input:
				io_conf.mode = GPIO_MODE_INPUT;
			break;

			// Input stays enabled on outputs: gpio_get_level() returns 0 on output-only pads.
			case Mode::Output:
				io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
			break;

			case Mode::OpenDrain:
				io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
			break;

			case Mode::InterruptRising:
				io_conf.mode = GPIO_MODE_INPUT;
				io_conf.intr_type = GPIO_INTR_POSEDGE;
				isInterruptMode = true;
			break;

			case Mode::InterruptFalling:
				io_conf.mode = GPIO_MODE_INPUT;
				io_conf.intr_type = GPIO_INTR_NEGEDGE;
				isInterruptMode = true;
			break;

			case Mode::InterruptRisingFalling:
				io_conf.mode = GPIO_MODE_INPUT;
				io_conf.intr_type = GPIO_INTR_ANYEDGE;
				isInterruptMode = true;
			break;

			case Mode::Analog:
				io_conf.mode = GPIO_MODE_DISABLE;
			break;

			default:
				return ResultStatus::notSupported;
		}

		switch (parameters.pull) {
			case Pull::Up:
				io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
				io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
			break;

			case Pull::Down:
				io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
				io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
			break;

			case Pull::None:
			default:
				io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
				io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
			break;
		}

		DetachInterrupt();

		if (gpio_config(&io_conf) != ESP_OK) {
			return ResultStatus::error;
		}

		if (isInterruptMode) {
			ResultStatus interruptStatus = InterruptInitialization();
			if (interruptStatus != ResultStatus::ok) {
				return interruptStatus;
			}

			ResultStatus attachStatus = AttachInterrupt();
			if (attachStatus != ResultStatus::ok) {
				return attachStatus;
			}
		}

		return AfterInitialization();
	}


private:
	ResultStatus AttachInterrupt() {
		if (!isrServiceInstalled) {
			esp_err_t serviceStatus = gpio_install_isr_service(0);
			if (serviceStatus != ESP_OK && serviceStatus != ESP_ERR_INVALID_STATE) {
				return ResultStatus::error;
			}

			isrServiceInstalled = true;
		}

		if (gpio_isr_handler_add(static_cast<gpio_num_t>(pin), InterruptHandler, this) != ESP_OK) {
			return ResultStatus::error;
		}

		interruptAttached = true;
		return ResultStatus::ok;
	}


	void DetachInterrupt() {
		if (interruptAttached) {
			gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
			interruptAttached = false;
		}
	}


	// Deliberately not IRAM_ATTR: the ISR service is installed without ESP_INTR_FLAG_IRAM, and an in-class function
	// placed in IRAM fails to link on Xtensa as soon as it needs a literal that the linker cannot relax away.
	static void InterruptHandler(void *argument) {
		GPIOAdapterESP *adapter = static_cast<GPIOAdapterESP *>(argument);
		adapter->IrqHandler();
	}
};
