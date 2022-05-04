#pragma once
#include <BSP/PortAdapters.h>



class RS485 : public AUART {
private:
	GPIOAdapter *dePin;

public:
	RS485() { }

	RS485(USART_TypeDef *uart, GPIOAdapter &de, uint32 baudRate = 115200):AUART(uart), dePin(&de) {
		dePin->Set(false);
		dePin->SetParameters({ GPIOAdapter::Direction::Output });

		SetParameters({ baudRate });
	}


protected:

	virtual Status::statusType WriteByteArray(uint8* buffer, uint32 size) override {
		dePin->Set(true);
		auto status = AUART::WriteByteArray(buffer, size);
		dePin->Set(false);
		return status;
	}


	virtual Status::statusType ReadByteArray(uint8* buffer, uint32 size) override {
		return AUART::ReadByteArray(buffer, size);
	}


	virtual Status::statusType WriteByteArrayAsync(uint8* buffer, uint32 size) override {
		dePin->Set(true);
		return AUART::WriteByteArrayAsync(buffer, size);
	}


	virtual Status::statusType ReadByteArrayAsync(uint8* buffer, uint32 size) override {
		return AUART::ReadByteArrayAsync(buffer, size);
	}



	virtual void CallInterrupt(Irq irqType) override {
Поскольку
		if(irqType == Irq::Tx) {
			dePin->Set(false);
		}
		AUART::CallInterrupt(irqType);
	 }
};


