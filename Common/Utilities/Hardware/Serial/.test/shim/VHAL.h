#pragma once
// Host stand-in for the peripheral part of the VHAL umbrella header, for the serial host tests. The UART is a class that keeps its
// interrupt handler and the last received byte, and a test raises a receive interrupt with Receive, as the peripheral does.
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <functional>


// The ring buffer enters a critical section around each access. The host run has one thread, so the section is a no-op.
inline void System::CriticalSection(bool isEnable) {
	(void)isEnable;
}


class AUART {
public:
	enum class Irq {
		Rx,
		Tx
	};


	void SetOnInterrupt(const std::function<void(Irq)>& handler) {
		onInterrupt = handler;
	}


	uint8 GetLastRxData() const {
		return rxData;
	}


	// Stores the byte and raises the receive interrupt.
	void Receive(uint8 data) {
		rxData = data;
		if (onInterrupt) {
			onInterrupt(Irq::Rx);
		}
	}


private:
	std::function<void(Irq)> onInterrupt;
	uint8 rxData = 0;
};
