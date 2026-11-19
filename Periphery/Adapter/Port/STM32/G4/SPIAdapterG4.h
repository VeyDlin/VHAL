#pragma once
#include <Adapter/SPIAdapter.h>
#include <cstring>


// SPI adapter for STM32G4: SPI with the RX FIFO. FRXTH sets the RXNE level and DS the frame size.
//
// What the BSP must do:
//  - Enable the clock of the instance before SetParameters(): APB2ENR SPI1EN (SPI1), APB1ENR1 SPI2EN (SPI2), APB1ENR1
//    SPI3EN (SPI3, only where the chip has it), APB2ENR SPI4EN (SPI4, only on STM32G471, G473, G474, G483 and G484).
//    The adapter never configures the RCC tree. While the clock is off, Initialization() returns notReady.
//  - Configure SCK, MOSI and MISO as the alternate function of the instance. The chip select pin is passed in the
//    parameters as a GPIOAdapter, and the base class drives it. Receive-only (Direction::Rx) reads MISO.
//  - Pass the APB bus clock the instance sits on as busClockHz: APB2 for SPI1 and SPI4, APB1 for SPI2 and SPI3. The
//    prescaler is taken from it. With 0 the slowest divider is used.
//  - Forward SPI1_IRQn and SPI2_IRQn to IrqHandler() of the instance, plus SPI3_IRQn (only where the chip has SPI3) and
//    SPI4_IRQn (only where the chip has SPI4), and enable them in NVIC. The adapter has no NVIC access.
//
// Frames: the frame size is not part of Parameters, so it is set with SetDataWidth() (8 bits by default). The interface
// counts bytes, so with 16-bit frames the byte count must be even, and each frame is two consecutive bytes in memory
// order (the first byte is the low byte). The base class counters are 16-bit, so one asynchronous transfer carries at
// most 65535 bytes.
//
// Full duplex clocks every frame in as well as out. The receive side is therefore drained on writes too, otherwise the
// RX FIFO overruns and the transfer ends with an error. Receive-only clocks nothing from this side and writes no dummy
// frames.


using ASPI = class SPIAdapterG4;


class SPIAdapterG4: public SPIAdapter<SPI_TypeDef> {
public:
	struct DataWidthOption: IOption<uint32> {
		using IOption::IOption;
	};


	struct DataWidth {
		static inline constexpr DataWidthOption Bits8   { LL_SPI_DATAWIDTH_8BIT };
		static inline constexpr DataWidthOption Bits16  { LL_SPI_DATAWIDTH_16BIT };
	};


private:
	static constexpr uint16 DummyFrame8 = 0xFF;
	static constexpr uint16 DummyFrame16 = 0xFFFF;
	static constexpr uint32 MaxAsyncSize = 0xFFFF;
	static constexpr uint32 FrameEndSpinLimit = 1000000;

	DataWidthOption dataWidth { LL_SPI_DATAWIDTH_8BIT };
	uint16 lastRxFrame = 0;
	bool continuousRx = false;
	bool isInitialized = false;
	Irq asyncIrq = Irq::TxRx;


public:
	SPIAdapterG4() = default;
	SPIAdapterG4(SPI_TypeDef *spi, uint32 busClockHz = 0): SPIAdapter(spi, busClockHz) { }


	// Frame size of the next configuration. Parameters has no field for it, so the port carries it. When the adapter is
	// already initialized the instance is reconfigured at once, which needs an idle bus.
	ResultStatus SetDataWidth(DataWidthOption width) {
		if(txState != ResultStatus::ready || rxState != ResultStatus::ready || continuousRx) {
			return ResultStatus::busy;
		}

		dataWidth = width;

		if(!isInitialized) {
			return ResultStatus::ok;
		}

		return Initialization();
	}


	virtual void IrqHandler() override {
		ErrorInterrupt();
		ReceiveInterrupt();
		TransmitInterrupt();
	}


	virtual void AbortReceive() override {
		AbortAllTransfers();
	}


	virtual void AbortTransmit() override {
		AbortAllTransfers();
	}


	virtual uint16 GetLastRxData() override {
		return lastRxFrame;
	}


	// Parameters are only installed while the instance is idle. The base class would store them before Initialization()
	// refuses a busy instance, and an in-flight transfer would then run with settings it did not start with.
	virtual Result<uint32> SetParameters(Parameters val) override {
		if(txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		return SPIAdapter::SetParameters(val);
	}


protected:
	virtual ResultStatus Initialization() override {
		// The state cleared below belongs to a transfer in flight, so the instance is only reconfigured while it is idle.
		if(txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		ResultStatus status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}

		if(!IsPeripheralClockEnabled()) {
			return ResultStatus::notReady;
		}

		StopAsyncState();

		// LL_SPI_Init only accepts a disabled instance, and the bus must be idle before it is disabled.
		if(LL_SPI_IsEnabled(spiHandle)) {
			status = WaitNotBusy();
			if(status != ResultStatus::ok) {
				return status;
			}

			LL_SPI_Disable(spiHandle);
		}

		LL_SPI_InitTypeDef init = {
			.TransferDirection = CastTransferDirection(),
			.Mode = CastMode(),
			.DataWidth = dataWidth.Get(),
			.ClockPolarity = CastClockPolarity(),
			.ClockPhase = CastClockPhase(),
			.NSS = LL_SPI_NSS_SOFT,
			.BaudRate = baudRatePrescaler,
			.BitOrder = CastBitOrder(),
			.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
			.CRCPoly = 7
		};

		if(LL_SPI_Init(spiHandle, &init) != ErrorStatus::SUCCESS) {
			return ResultStatus::error;
		}

		// Software NSS: a master needs SSI set, otherwise it sees its own NSS as pulled low and raises a mode fault.
		// LL_SPI_Init clears SSI, so it is set again here for a master.
		if(parameters.mode == Mode::Master) {
			SET_BIT(spiHandle->CR1, SPI_CR1_SSI);
		} else {
			CLEAR_BIT(spiHandle->CR1, SPI_CR1_SSI);
		}

		LL_SPI_SetStandard(spiHandle, LL_SPI_PROTOCOL_MOTOROLA);

		// LL_SPI_Init only sets the quarter threshold for 8-bit frames, so the threshold is always set here.
		LL_SPI_SetRxFIFOThreshold(spiHandle, CastRxFifoThreshold());

		ClearErrorFlags();
		LL_SPI_Enable(spiHandle);

		isInitialized = true;

		return AfterInitialization();
	}


	// Returns the bit clock in Hz, or 0 when the bus clock is unknown. The divider is the fastest one whose result does
	// not exceed maxSpeedHz.
	virtual uint32 CalculatePrescaler() override {
		static constexpr uint8 baudRateCount = 8;
		static constexpr uint32 baudRateValues[baudRateCount] = {
			LL_SPI_BAUDRATEPRESCALER_DIV2,
			LL_SPI_BAUDRATEPRESCALER_DIV4,
			LL_SPI_BAUDRATEPRESCALER_DIV8,
			LL_SPI_BAUDRATEPRESCALER_DIV16,
			LL_SPI_BAUDRATEPRESCALER_DIV32,
			LL_SPI_BAUDRATEPRESCALER_DIV64,
			LL_SPI_BAUDRATEPRESCALER_DIV128,
			LL_SPI_BAUDRATEPRESCALER_DIV256
		};

		// Without a known bus clock or a speed limit the slowest divider is the only safe choice.
		uint8 index = baudRateCount - 1;

		if(inputBusClockHz != 0 && parameters.maxSpeedHz != 0) {
			for(uint8 i = 0; i < baudRateCount; i++) {
				if(inputBusClockHz / (2u << i) <= parameters.maxSpeedHz) {
					index = i;
					break;
				}
			}
		}

		baudRatePrescaler = baudRateValues[index];

		if(inputBusClockHz == 0) {
			return 0;
		}

		return inputBusClockHz / (2u << index);
	}


	virtual ResultStatus WriteByteArray(uint8 *buffer, uint32 size) override {
		if(!SupportsTx()) {
			return ResultStatus::notSupported;
		}

		return TransferBlocking(buffer, nullptr, size);
	}


	virtual ResultStatus ReadByteArray(uint8 *buffer, uint32 size) override {
		if(!SupportsRx()) {
			return ResultStatus::notSupported;
		}

		return TransferBlocking(nullptr, buffer, size);
	}


	virtual ResultStatus WriteReadByteArray(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		if(parameters.direction != Direction::TxRx) {
			return ResultStatus::notSupported;
		}

		return TransferBlocking(txBuffer, rxBuffer, size);
	}


	virtual ResultStatus WriteByteArrayAsync(uint8 *buffer, uint32 size) override {
		if(!SupportsTx()) {
			return ResultStatus::notSupported;
		}

		return StartAsync(buffer, nullptr, size, Irq::Tx);
	}


	// Full duplex clocks dummy frames while the buffer is filled. Receive-only has no transmit side to clock them.
	virtual ResultStatus ReadByteArrayAsync(uint8 *buffer, uint32 size) override {
		if(!SupportsRx()) {
			return ResultStatus::notSupported;
		}

		return StartAsync(nullptr, buffer, size, Irq::Rx);
	}


	virtual ResultStatus WriteReadByteArrayAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		if(parameters.direction != Direction::TxRx) {
			return ResultStatus::notSupported;
		}

		return StartAsync(txBuffer, rxBuffer, size, Irq::TxRx);
	}


	// Continuous receive has no transmit side of its own, so the frames come from whoever clocks the bus. A master would
	// have to clock the bus itself, which this port does not do, so it is refused instead of leaving the instance locked.
	virtual ResultStatus StartContinuousAsyncRxMode() override {
		if(!SupportsRx()) {
			return ResultStatus::notSupported;
		}

		if(parameters.mode == Mode::Master) {
			return ResultStatus::notSupported;
		}

		if(txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		// The flags are set before the interrupts are enabled, so an interrupt that arrives at once sees a consistent state.
		continuousRx = true;
		continuousAsyncRxMode = true;
		rxState = ResultStatus::busy;
		rxDataPointer = nullptr;
		rxDataNeed = 0;
		rxDataCounter = 0;
		asyncIrq = Irq::Rx;

		LL_SPI_EnableIT_RXNE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle);

		return ResultStatus::ok;
	}


	virtual ResultStatus StopContinuousAsyncRxMode() override {
		if(!continuousRx) {
			return ResultStatus::ok;
		}

		AbortAllTransfers();

		return ResultStatus::ok;
	}


private:
	bool IsPeripheralClockEnabled() const {
		if(spiHandle == SPI1) {
			return LL_APB2_GRP1_IsEnabledClock(LL_APB2_GRP1_PERIPH_SPI1);
		}

		if(spiHandle == SPI2) {
			return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_SPI2);
		}

#if defined(SPI3)
		if(spiHandle == SPI3) {
			return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_SPI3);
		}
#endif

#if defined(SPI4)
		if(spiHandle == SPI4) {
			return LL_APB2_GRP1_IsEnabledClock(LL_APB2_GRP1_PERIPH_SPI4);
		}
#endif

		return false;
	}


	bool SupportsTx() const {
		return parameters.direction != Direction::Rx;
	}


	bool SupportsRx() const {
		return parameters.direction != Direction::Tx;
	}


	uint32 FrameBytes() const {
		return dataWidth == DataWidth::Bits16 ? 2u : 1u;
	}


	// Dummy frames are sent when there is no source, which clocks in data during a read.
	uint16 LoadFrame(const uint8 *source) const {
		if(dataWidth == DataWidth::Bits16) {
			uint16 frame = DummyFrame16;
			if(source != nullptr) {
				std::memcpy(&frame, source, sizeof(frame));
			}

			return frame;
		}

		return source == nullptr ? DummyFrame8 : *source;
	}


	void StoreFrame(uint8 *destination, uint16 frame) const {
		if(dataWidth == DataWidth::Bits16) {
			std::memcpy(destination, &frame, sizeof(frame));
			return;
		}

		*destination = static_cast<uint8>(frame);
	}


	// The data register is accessed with the width of the frame: a 16-bit access in 8-bit mode would take two frames.
	void TransmitFrame(const uint8 *source) {
		uint16 frame = LoadFrame(source);

		if(dataWidth == DataWidth::Bits16) {
			LL_SPI_TransmitData16(spiHandle, frame);
		} else {
			LL_SPI_TransmitData8(spiHandle, static_cast<uint8>(frame));
		}
	}


	uint16 ReceiveFrame() {
		if(dataWidth == DataWidth::Bits16) {
			return LL_SPI_ReceiveData16(spiHandle);
		}

		return LL_SPI_ReceiveData8(spiHandle);
	}


	// Reads and clears the error flags. Overrun is checked last, so it wins when several flags are set.
	Error ClearErrorFlags() {
		Error error = Error::None;

		if(LL_SPI_IsActiveFlag_FRE(spiHandle)) {
			LL_SPI_ClearFlag_FRE(spiHandle);
			error = Error::FrameFormat;
		}

		if(LL_SPI_IsActiveFlag_MODF(spiHandle)) {
			// LL_SPI_ClearFlag_MODF clears SPE as well, so the instance is enabled again or it stays silent for good.
			LL_SPI_ClearFlag_MODF(spiHandle);
			LL_SPI_Enable(spiHandle);
			error = Error::ModeFault;
		}

		if(LL_SPI_IsActiveFlag_OVR(spiHandle)) {
			LL_SPI_ClearFlag_OVR(spiHandle);
			error = Error::Overrun;
		}

		return error;
	}


	ResultStatus WaitNotBusy() {
		uint64 tickStart = System::GetTick();

		while(LL_SPI_IsActiveFlag_BSY(spiHandle)) {
			if((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}

		return ResultStatus::ok;
	}


	// The last frame has left the bus once TXE is set (no frame waits in the TX buffer) and BSY is clear. BSY alone is not
	// enough, because it is still clear right after the last data register write. The loop is bounded by iterations, not
	// ticks, because the interrupt calls it and the tick may not advance there.
	ResultStatus WaitFrameEnd() {
		uint32 spins = 0;

		while(!LL_SPI_IsActiveFlag_TXE(spiHandle) || LL_SPI_IsActiveFlag_BSY(spiHandle)) {
			spins++;

			if(spins > FrameEndSpinLimit) {
				return ResultStatus::timeout;
			}
		}

		return ResultStatus::ok;
	}


	// Brings the instance to rest after an abort or an error, so the next transfer does not read this one's frames. A master
	// lets the frame in flight finish first. A slave cannot wait for the master's clock, so it only reads out what is
	// already in the RX FIFO. Without the drain the next transfer reads the stale echo as its first frame, and every frame
	// after it is misaligned.
	void DrainFrames() {
		if(SupportsTx() && parameters.mode == Mode::Master) {
			WaitFrameEnd();
		}

		uint32 spins = 0;

		while(LL_SPI_IsActiveFlag_RXNE(spiHandle) && spins < FrameEndSpinLimit) {
			ReceiveFrame();
			spins++;
		}

		ClearErrorFlags();
	}


	// Polled transfer. Each received frame is stored into rxBuffer, or dropped when there is none. Dummy frames are sent
	// when txBuffer is nullptr and the direction transmits.
	ResultStatus TransferBlocking(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) {
		if(txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		uint32 frameBytes = FrameBytes();
		if(size % frameBytes != 0) {
			return ResultStatus::invalidParameter;
		}

		uint32 frames = size / frameBytes;
		uint32 txLeft = SupportsTx() ? frames : 0;
		uint32 rxLeft = SupportsRx() ? frames : 0;
		uint32 txIndex = 0;
		uint32 rxIndex = 0;
		uint64 tickStart = System::GetTick();

		// Full duplex returns one frame per frame sent, so the receive side is drained even when nothing is stored.
		while(txLeft > 0 || rxLeft > 0) {
			if(txLeft > 0 && LL_SPI_IsActiveFlag_TXE(spiHandle)) {
				TransmitFrame(txBuffer == nullptr ? nullptr : txBuffer + txIndex * frameBytes);
				txIndex++;
				txLeft--;
			}

			if(LL_SPI_IsActiveFlag_RXNE(spiHandle)) {
				uint16 frame = ReceiveFrame();
				lastRxFrame = frame;
				lastRxData = static_cast<uint8>(frame);

				if(rxLeft > 0) {
					if(rxBuffer != nullptr) {
						StoreFrame(rxBuffer + rxIndex * frameBytes, frame);
					}

					rxIndex++;
					rxLeft--;
				}
			}

			if(ClearErrorFlags() != Error::None) {
				DrainFrames();
				return ResultStatus::error;
			}

			// The SPI stays enabled on timeout, as it does between transfers.
			if((System::GetTick() - tickStart) > timeout) {
				DrainFrames();
				return ResultStatus::timeout;
			}
		}

		if(SupportsTx() && WaitFrameEnd() != ResultStatus::ok) {
			DrainFrames();
			return ResultStatus::timeout;
		}

		return ResultStatus::ok;
	}


	// Starts an interrupt-driven transfer. Both sides are armed before their interrupts are enabled. The transfer ends
	// when the receive side has its last frame, because a full-duplex write only finishes once its echo is drained. A side
	// that the direction does not use stays ready.
	ResultStatus StartAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size, Irq irqType) {
		if(size == 0 || size % FrameBytes() != 0) {
			return ResultStatus::invalidParameter;
		}

		if(size > MaxAsyncSize) {
			return ResultStatus::outOfRange;
		}

		if(txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		asyncIrq = irqType;

		if(SupportsTx()) {
			txState = ResultStatus::busy;
			txDataPointer = txBuffer;
			txDataNeed = static_cast<uint16>(size);
			txDataCounter = 0;
		}

		if(SupportsRx()) {
			rxState = ResultStatus::busy;
			rxDataPointer = rxBuffer;
			rxDataNeed = static_cast<uint16>(size);
			rxDataCounter = 0;

			LL_SPI_EnableIT_RXNE(spiHandle);
		}

		LL_SPI_EnableIT_ERR(spiHandle);

		if(SupportsTx()) {
			LL_SPI_EnableIT_TXE(spiHandle);
		}

		return ResultStatus::ok;
	}


	void StopAsyncState() {
		LL_SPI_DisableIT_TXE(spiHandle);
		LL_SPI_DisableIT_RXNE(spiHandle);
		LL_SPI_DisableIT_ERR(spiHandle);

		txState = ResultStatus::ready;
		txDataPointer = nullptr;
		txDataNeed = 0;
		txDataCounter = 0;

		rxState = ResultStatus::ready;
		rxDataPointer = nullptr;
		rxDataNeed = 0;
		rxDataCounter = 0;

		continuousRx = false;
		continuousAsyncRxMode = false;
	}


	// The frame stream is shared by both directions, so any abort stops the whole transfer, including continuous receive.
	// The state is cleared with interrupts masked, so an interrupt cannot finish the transfer after the caller aborted it.
	void AbortAllTransfers() {
		uint32 primask = __get_PRIMASK();
		__disable_irq();
		StopAsyncState();
		__set_PRIMASK(primask);

		DrainFrames();
		ChipSelect(false);
	}


	void FinishTransfer() {
		StopAsyncState();
		ChipSelect(false);
		CallInterrupt(asyncIrq);
	}


	inline void TransmitInterrupt() {
		if(!LL_SPI_IsEnabledIT_TXE(spiHandle) || !LL_SPI_IsActiveFlag_TXE(spiHandle)) {
			return;
		}

		TransmitFrame(txDataPointer == nullptr ? nullptr : txDataPointer + txDataCounter);
		txDataCounter = static_cast<uint16>(txDataCounter + FrameBytes());

		if(txDataCounter < txDataNeed) {
			return;
		}

		LL_SPI_DisableIT_TXE(spiHandle);

		// A full-duplex write finishes on its echo in the receive interrupt, and receive-only never gets here.
		if(SupportsRx()) {
			return;
		}

		// Only a master drives chip select, so only a master holds the interrupt until the last frame has left the bus. A
		// slave has nothing to release, and waiting on a master that may have stopped clocking would hang the interrupt.
		if(parameters.mode == Mode::Slave) {
			FinishTransfer();
			return;
		}

		// The interface has no timeout error. A bus that does not go idle is reported as a mode fault, the closest of its
		// errors, and the transfer ends without a completion.
		if(WaitFrameEnd() != ResultStatus::ok) {
			StopAsyncState();
			ChipSelect(false);
			CallError(Error::ModeFault);
			return;
		}

		FinishTransfer();
	}


	inline void ReceiveInterrupt() {
		while(LL_SPI_IsEnabledIT_RXNE(spiHandle) && LL_SPI_IsActiveFlag_RXNE(spiHandle)) {
			uint16 frame = ReceiveFrame();
			lastRxFrame = frame;
			lastRxData = static_cast<uint8>(frame);

			// A continuous receive reports every frame and never ends by itself.
			if(continuousRx) {
				CallInterrupt(Irq::Rx);
				continue;
			}

			// The receive pointer is null for the echo of a write, which is drained without storing.
			if(rxDataPointer != nullptr) {
				StoreFrame(rxDataPointer + rxDataCounter, frame);
			}

			rxDataCounter = static_cast<uint16>(rxDataCounter + FrameBytes());

			if(rxDataCounter >= rxDataNeed) {
				FinishTransfer();
				return;
			}
		}
	}


	inline void ErrorInterrupt() {
		if(!LL_SPI_IsEnabledIT_ERR(spiHandle)) {
			return;
		}

		Error error = ClearErrorFlags();
		if(error == Error::None) {
			return;
		}

		// An overrun drops frames, so the counters of the transfer could never reach their end. The transfer is aborted.
		// A continuous receive keeps running and only reports the error.
		if(!continuousRx) {
			AbortAllTransfers();
		}

		CallError(error);
	}


	uint32 CastTransferDirection() const {
		switch(parameters.direction) {
			case Direction::Tx: {
				return LL_SPI_HALF_DUPLEX_TX;
			}
			case Direction::Rx: {
				// Receive-only reads MISO. The 1-line bidirectional mode would read MOSI in master mode instead.
				return LL_SPI_SIMPLEX_RX;
			}
			case Direction::TxRx: {
				return LL_SPI_FULL_DUPLEX;
			}
		}

		SystemAbort();
		return 0;
	}


	uint32 CastMode() const {
		switch(parameters.mode) {
			case Mode::Master: {
				return LL_SPI_MODE_MASTER;
			}
			case Mode::Slave: {
				return LL_SPI_MODE_SLAVE;
			}
		}

		SystemAbort();
		return 0;
	}


	uint32 CastClockPolarity() const {
		switch(parameters.clockPolarity) {
			case ClockPolarity::High: {
				return LL_SPI_POLARITY_HIGH;
			}
			case ClockPolarity::Low: {
				return LL_SPI_POLARITY_LOW;
			}
		}

		SystemAbort();
		return 0;
	}


	uint32 CastClockPhase() const {
		switch(parameters.clockPhase) {
			case ClockPhase::Edge1: {
				return LL_SPI_PHASE_1EDGE;
			}
			case ClockPhase::Edge2: {
				return LL_SPI_PHASE_2EDGE;
			}
		}

		SystemAbort();
		return 0;
	}


	uint32 CastBitOrder() const {
		switch(parameters.firstBit) {
			case FirstBit::LSB: {
				return LL_SPI_LSB_FIRST;
			}
			case FirstBit::MSB: {
				return LL_SPI_MSB_FIRST;
			}
		}

		SystemAbort();
		return 0;
	}


	// RXNE must fire once per frame. A quarter of the FIFO holds one 8-bit frame, a half holds one 16-bit frame.
	uint32 CastRxFifoThreshold() const {
		return dataWidth == DataWidth::Bits16 ? LL_SPI_RX_FIFO_TH_HALF : LL_SPI_RX_FIFO_TH_QUARTER;
	}
};
