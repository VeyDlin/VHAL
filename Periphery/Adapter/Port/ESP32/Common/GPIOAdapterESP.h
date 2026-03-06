#pragma once
#include <Adapter/GPIOAdapter.h>


using AGPIO = class GPIOAdapterESP;


class GPIOAdapterESP : public GPIOAdapter<> {
public:
	using GPIOAdapter<>::operator=;

	GPIOAdapterESP() = default;
	GPIOAdapterESP(uint8 gpioPin, bool gpioInversion = false) : GPIOAdapter(gpioPin, gpioInversion) { }


protected:
	virtual inline bool GetPinState() override {
		return static_cast<bool>(gpio_get_level(static_cast<gpio_num_t>(pin)));
	}


	virtual inline void SetPinState(bool state) override {
		gpio_set_level(static_cast<gpio_num_t>(pin), state ? 1 : 0);
	}


	virtual inline void TogglePinState() override {
		SetPinState(!GetPinState());
	}


	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		gpio_config_t io_conf = {};
		io_conf.pin_bit_mask = (1ULL << pin);
		io_conf.intr_type = GPIO_INTR_DISABLE;

		switch (parameters.mode) {
			case Mode::Input:
				io_conf.mode = GPIO_MODE_INPUT;
			break;

			case Mode::Output:
				io_conf.mode = GPIO_MODE_OUTPUT;
			break;

			case Mode::OpenDrain:
				io_conf.mode = GPIO_MODE_OUTPUT_OD;
			break;

			case Mode::InterruptRising:
				io_conf.mode = GPIO_MODE_INPUT;
				io_conf.intr_type = GPIO_INTR_POSEDGE;
			break;

			case Mode::InterruptFalling:
				io_conf.mode = GPIO_MODE_INPUT;
				io_conf.intr_type = GPIO_INTR_NEGEDGE;
			break;

			case Mode::InterruptRisingFalling:
				io_conf.mode = GPIO_MODE_INPUT;
				io_conf.intr_type = GPIO_INTR_ANYEDGE;
			break;

			default:
				io_conf.mode = GPIO_MODE_DISABLE;
			break;
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

		if (gpio_config(&io_conf) != ESP_OK) {
			return Status::error;
		}

		return AfterInitialization();
	}
};
