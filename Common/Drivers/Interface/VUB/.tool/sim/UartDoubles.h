#pragma once
#include "LinkFaults.h"
#include <VHAL.h>
#include <Adapter/UARTAdapter.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// The handle the fake port is built on. The base class keeps a pointer to it and never reads through it.
struct FakeUartHandle {
};


// A UART port made of the real UARTAdapter base, so the callbacks, the continuous receive and the transfer state are the base's own.
// The port's bytes go to its peer, a second FakeUart: what one port writes, the other receives while its continuous receive is on, and
// loses otherwise.
//
// Two behaviours. The G4 port (completesInWrite false) runs a transfer until FinishTransmit, which delivers the bytes and raises Irq::Tx.
// The ESP32 port (completesInWrite true) delivers the bytes and raises Irq::Tx inside WriteArrayAsync, before it returns.
class FakeUart : public UARTAdapter<FakeUartHandle> {
public:
	FakeUart() = default;


	// The port of a handle, as the G4 adapter is made from its USART_TypeDef.
	explicit FakeUart(FakeUartHandle* handle) : UARTAdapter<FakeUartHandle>(handle) {
	}


	FakeUart* peer = nullptr;
	bool completesInWrite = false;
	uint32 transfersStarted = 0;
	uint32 transfersAborted = 0;
	uint32 bytesLost = 0;
	uint32 rxStarts = 0;
	uint32 rxStops = 0;
	LinkFaults faults;


	// The receive interrupt of one byte: the port's last byte, then Irq::Rx. The test plays the wire.
	void RaiseRx(uint8 byte) {
		lastRxData = byte;
		CallInterrupt(Irq::Rx);
	}


	void RaiseError(Error error) {
		CallError(error);
	}


	// The end of a transfer started with WriteArrayAsync: the bytes go to the peer, then Irq::Tx. Does nothing when no transfer runs.
	void FinishTransmit() {
		if (txState != ResultStatus::busy) {
			return;
		}

		DeliverPending();
		txState = ResultStatus::ready;
		CallInterrupt(Irq::Tx);
	}


	// A byte from the peer. It reaches the receive interrupt while the continuous receive is on.
	void ReceiveByte(uint8 byte) {
		if (continuousAsyncRxMode) {
			RaiseRx(byte);
			return;
		}

		bytesLost++;
	}


	void IrqHandler() override {
	}


	void AbortReceive() override {
	}


	// As on the G4 port, an aborted transfer ends without Irq::Tx and leaves the port ready.
	void AbortTransmit() override {
		if (txState == ResultStatus::busy) {
			transfersAborted++;
		}

		txState = ResultStatus::ready;
	}


protected:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


	ResultStatus WriteByteArray(uint8* buffer, uint32 size) override {
		(void)buffer;
		(void)size;
		return ResultStatus::notSupported;
	}


	ResultStatus ReadByteArray(uint8* buffer, uint32 size) override {
		(void)buffer;
		(void)size;
		return ResultStatus::notSupported;
	}


	ResultStatus WriteByteArrayAsync(uint8* buffer, uint32 size) override {
		if (txState == ResultStatus::busy) {
			return ResultStatus::busy;
		}

		txState = ResultStatus::busy;
		txDataPointer = buffer;
		txDataNeed = size;
		txDataCounter = 0;
		transfersStarted++;
		if (completesInWrite) {
			DeliverPending();
			txState = ResultStatus::ready;
			CallInterrupt(Irq::Tx);
		}

		return ResultStatus::ok;
	}


	ResultStatus ReadByteArrayAsync(uint8* buffer, uint32 size) override {
		(void)buffer;
		(void)size;
		return ResultStatus::notSupported;
	}


	ResultStatus StartContinuousAsyncRxMode() override {
		rxStarts++;
		return ResultStatus::ok;
	}


	ResultStatus StopContinuousAsyncRxMode() override {
		rxStops++;
		return ResultStatus::ok;
	}


private:
	void DeliverPending() {
		if (peer == nullptr) {
			bytesLost = bytesLost + txDataNeed;
			return;
		}

		for (uint32 index = 0; index < txDataNeed; index++) {
			uint8 value = txDataPointer[index];
			if (faults.Byte(value) != LinkFaults::Fate::drop) {
				peer->ReceiveByte(value);
			}
		}
	}
};
