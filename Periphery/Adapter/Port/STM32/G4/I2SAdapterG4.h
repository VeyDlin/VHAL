#pragma once
// I2S port for STM32G4 (G431/G441/G471/G474/G484 and siblings). The SPI block runs in I2S mode and this adapter drives
// its registers and prescaler. It never touches the RCC clock tree.
//
// Instances: the CMSIS macro IS_I2S_ALL_INSTANCE decides which SPI blocks have I2S. G431 and G474 have SPI2 and SPI3;
// G411 has SPI2 only. Other instances make SetParameters return notSupported.
//
// What the BSP must do:
//  - Enable the bus clock of the instance before SetParameters: RCC_APB1ENR1 SPI2EN/SPI3EN. The adapter checks the clock
//    and returns notReady when it is off.
//  - Select the I2S kernel clock source in RCC (RM0440, I2S clock selection) and pass its frequency in Hz as busClockHz.
//    Master mode needs it; slave mode accepts 0.
//  - Route SCK, WS, SD and MCK to the instance through the chip's alternate-function table.
//  - Enable the SPIx interrupt in the NVIC (SPI2_IRQn or SPI3_IRQn) and call IrqHandler() from that handler.
//
// Data model: buffers are arrays of 16-bit words and size counts words, as the I2SAdapter contract requires. Every word
// is one DR access. A 16-bit sample is one word. A 24-bit or 32-bit sample is two consecutive words, sent in buffer
// order (the same sequence the ST HAL uses). Async and circular transfers need size to cover whole frames in both
// halves: a multiple of twice the frame word count (4 words for 16-bit data, 8 for 24/32-bit data), so the half-way
// notice lands on a frame boundary. Sync transfers have no such requirement.
//
// Not supported: Standard::PDM (a separate block on these chips), Direction::TxRx (the block carries one direction),
// TDM and mono (the I2S block always carries two channels).


#include <Adapter/I2SAdapter.h>


#if defined(SPI_I2S_SUPPORT)


using AI2S = class I2SAdapterG4;


class I2SAdapterG4: public I2SAdapter<SPI_TypeDef> {
public:
	struct ClockSource {
		// The kernel clock is chosen by the BSP in RCC and reaches the adapter through busClockHz. This option exists so
		// portable code that names a clock source still compiles.
		static inline constexpr ClockSourceOption Default { 0 };
	};

	struct SlotCount {
		// The I2S block carries exactly two channels. 0 (left unset) is accepted and means the same as 2.
		static inline constexpr SlotCountOption S2 { 2 };
	};

	struct Slot {
		static inline constexpr SlotOption Stereo { (1u << 0) | (1u << 1) };
	};


private:
	uint16 *txBuffer = nullptr;
	uint32 txLength = 0;
	volatile uint32 txPosition = 0;
	bool txCircular = false;
	volatile ResultStatus txState = ResultStatus::ready;
	uint16 *rxBuffer = nullptr;
	uint32 rxLength = 0;
	volatile uint32 rxPosition = 0;
	bool rxCircular = false;
	volatile ResultStatus rxState = ResultStatus::ready;
	// Set once Initialization has written the hardware. From then on the hardware runs the requested configuration.
	bool hardwareConfigured = false;


public:
	I2SAdapterG4() {
		i2sHandle = nullptr;
	}


	I2SAdapterG4(SPI_TypeDef *i2s, uint32 busClockHz = 0): I2SAdapter(i2s, busClockHz) { }


	// The base class stores the request before Initialization validates it. A rejected request must not stay in
	// GetParameters() while the hardware keeps the previous configuration, so the previous set is restored. Once the
	// hardware has been reconfigured the new set stays, even if the AfterPeripheryInit hook then fails.
	ResultStatus SetParameters(Parameters val) override {
		Parameters previous = parameters;
		ResultStatus status = I2SAdapter<SPI_TypeDef>::SetParameters(val);
		if (status != ResultStatus::ok && !hardwareConfigured) {
			parameters = previous;
		}
		return status;
	}


	// Sync
	ResultStatus Transmit(uint16 *buffer, uint32 size) override {
		if (i2sHandle == nullptr) {
			return ResultStatus::notReady;
		}
		if (parameters.direction != Direction::Tx) {
			return ResultStatus::notSupported;
		}
		if (buffer == nullptr) {
			return ResultStatus::invalidParameter;
		}
		if (txState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		EnsureEnabled();

		uint64 tickStart = System::GetTick();
		for (uint32 i = 0; i < size; i++) {
			while (!LL_I2S_IsActiveFlag_TXE(i2sHandle)) {
				if (System::GetTick() - tickStart > timeout) {
					return ResultStatus::timeout;
				}
			}
			LL_I2S_TransmitData16(i2sHandle, buffer[i]);
		}

		return WaitTransmitIdle();
	}


	ResultStatus Receive(uint16 *buffer, uint32 size) override {
		if (i2sHandle == nullptr) {
			return ResultStatus::notReady;
		}
		if (parameters.direction != Direction::Rx) {
			return ResultStatus::notSupported;
		}
		if (buffer == nullptr) {
			return ResultStatus::invalidParameter;
		}
		if (rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		EnsureEnabled();

		uint64 tickStart = System::GetTick();
		for (uint32 i = 0; i < size; i++) {
			while (!LL_I2S_IsActiveFlag_RXNE(i2sHandle)) {
				if (System::GetTick() - tickStart > timeout) {
					return ResultStatus::timeout;
				}
			}

			// The sync path does not enable the error interrupt, so an overrun is reported here. The word just read is
			// stale and one word was lost. Clearing the flag reads DR and SR, which releases the block.
			if (LL_I2S_IsActiveFlag_OVR(i2sHandle)) {
				LL_I2S_ClearFlag_OVR(i2sHandle);
				return ResultStatus::error;
			}
			buffer[i] = LL_I2S_ReceiveData16(i2sHandle);
		}

		return ResultStatus::ok;
	}


	ResultStatus TransmitReceive(uint16 *, uint16 *, uint32) override {
		return ResultStatus::notSupported;
	}


	// Async (interrupt-driven)
	ResultStatus TransmitAsync(uint16 *buffer, uint32 size) override {
		return StartTransmit(buffer, size, false);
	}


	ResultStatus ReceiveAsync(uint16 *buffer, uint32 size) override {
		return StartReceive(buffer, size, false);
	}


	ResultStatus TransmitReceiveAsync(uint16 *, uint16 *, uint32) override {
		return ResultStatus::notSupported;
	}


	// Circular (continuous streaming, the ISR wraps the buffer until the matching Stop call)
	ResultStatus TransmitCircular(uint16 *buffer, uint32 size) override {
		return StartTransmit(buffer, size, true);
	}


	ResultStatus ReceiveCircular(uint16 *buffer, uint32 size) override {
		return StartReceive(buffer, size, true);
	}


	// Control
	// Blocks until the shift register has drained, so it must not be called from onInterrupt or onError: the wait would
	// spin inside the interrupt.
	ResultStatus StopTransmit() override {
		if (i2sHandle == nullptr) {
			return ResultStatus::notReady;
		}
		if (parameters.direction != Direction::Tx) {
			return ResultStatus::notSupported;
		}

		ResetTransfers();
		ResultStatus status = WaitTransmitIdle();

		// The block carries one direction, so stopping the transmitter stops the whole I2S block.
		LL_I2S_Disable(i2sHandle);
		return status;
	}


	ResultStatus StopReceive() override {
		if (i2sHandle == nullptr) {
			return ResultStatus::notReady;
		}
		if (parameters.direction != Direction::Rx) {
			return ResultStatus::notSupported;
		}

		ResetTransfers();
		LL_I2S_Disable(i2sHandle);
		return ResultStatus::ok;
	}


	void IrqHandler() override {
		ErrorInterrupt();
		TransmitInterrupt();
		ReceiveInterrupt();
	}


protected:
	ResultStatus Initialization() override {
		hardwareConfigured = false;

		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		status = ValidateParameters();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (parameters.mode == Mode::Master && inputBusClockHz == 0) {
			return ResultStatus::notReady;
		}

		if (!IsPeripheralClockEnabled()) {
			return ResultStatus::notReady;
		}

		// The prescaler is derived before the block is stopped, so a rejected rate leaves the running configuration intact.
		uint32 linearPrescaler = 2;
		uint32 parityPrescaler = LL_I2S_PRESCALER_PARITY_EVEN;
		if (parameters.mode == Mode::Master) {
			uint32 divider = 0;
			status = CalculateDivider(divider);
			if (status != ResultStatus::ok) {
				return status;
			}

			linearPrescaler = divider / 2;
			parityPrescaler = (divider % 2 != 0) ? LL_I2S_PRESCALER_PARITY_ODD : LL_I2S_PRESCALER_PARITY_EVEN;
		}

		// Reconfiguring would drop a transfer in flight without any callback, so refuse. The caller stops it first.
		if (txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		// The I2S configuration may only change while the block is disabled.
		ResetTransfers();
		LL_I2S_Disable(i2sHandle);

		LL_I2S_SetTransferMode(i2sHandle, CastMode());
		LL_I2S_SetStandard(i2sHandle, CastStandard());
		LL_I2S_SetDataFormat(i2sHandle, CastDataFormat());
		LL_I2S_SetClockPolarity(i2sHandle, CastClockPolarity());
		LL_I2S_SetPrescalerLinear(i2sHandle, static_cast<uint8>(linearPrescaler));
		LL_I2S_SetPrescalerParity(i2sHandle, parityPrescaler);

		// MCK is driven by the master only, so a slave leaves the pin alone.
		if (parameters.mode == Mode::Master && parameters.mclkOutput) {
			LL_I2S_EnableMasterClock(i2sHandle);
		} else {
			LL_I2S_DisableMasterClock(i2sHandle);
		}

		// Enabled once here, like the SPI port, so the bit clock keeps running between transfers.
		LL_I2S_Enable(i2sHandle);

		hardwareConfigured = true;
		return AfterInitialization();
	}


private:
	ResultStatus StartTransmit(uint16 *buffer, uint32 size, bool circular) {
		if (i2sHandle == nullptr) {
			return ResultStatus::notReady;
		}
		if (parameters.direction != Direction::Tx) {
			return ResultStatus::notSupported;
		}
		if (buffer == nullptr || size == 0) {
			return ResultStatus::invalidParameter;
		}
		if (size % (2 * FrameWords()) != 0) {
			return ResultStatus::invalidParameter;
		}
		if (txState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		EnsureEnabled();

		txBuffer = buffer;
		txLength = size;
		txPosition = 0;
		txCircular = circular;
		txState = ResultStatus::busy;

		// The ISR reads the transfer fields without a lock. The barrier makes the stores above visible before the
		// interrupts are enabled, so the ISR never sees a half-configured transfer.
		__DMB();
		LL_I2S_EnableIT_ERR(i2sHandle);
		LL_I2S_EnableIT_TXE(i2sHandle);
		return ResultStatus::ok;
	}


	ResultStatus StartReceive(uint16 *buffer, uint32 size, bool circular) {
		if (i2sHandle == nullptr) {
			return ResultStatus::notReady;
		}
		if (parameters.direction != Direction::Rx) {
			return ResultStatus::notSupported;
		}
		if (buffer == nullptr || size == 0) {
			return ResultStatus::invalidParameter;
		}
		if (size % (2 * FrameWords()) != 0) {
			return ResultStatus::invalidParameter;
		}
		if (rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		EnsureEnabled();

		rxBuffer = buffer;
		rxLength = size;
		rxPosition = 0;
		rxCircular = circular;
		rxState = ResultStatus::busy;

		// Same publication barrier as StartTransmit.
		__DMB();
		LL_I2S_EnableIT_ERR(i2sHandle);
		LL_I2S_EnableIT_RXNE(i2sHandle);
		return ResultStatus::ok;
	}


	void ResetTransfers() {
		LL_I2S_DisableIT_TXE(i2sHandle);
		LL_I2S_DisableIT_RXNE(i2sHandle);
		LL_I2S_DisableIT_ERR(i2sHandle);

		txState = ResultStatus::ready;
		rxState = ResultStatus::ready;
		txCircular = false;
		rxCircular = false;
	}


	void EnsureEnabled() {
		if (!LL_I2S_IsEnabled(i2sHandle)) {
			LL_I2S_Enable(i2sHandle);
		}
	}


	// Waits until the last written word has left the shift register. Nothing to wait for when the block is off.
	ResultStatus WaitTransmitIdle() {
		if (!LL_I2S_IsEnabled(i2sHandle)) {
			return ResultStatus::ok;
		}

		uint64 tickStart = System::GetTick();
		while (!LL_I2S_IsActiveFlag_TXE(i2sHandle)) {
			if (System::GetTick() - tickStart > timeout) {
				return ResultStatus::timeout;
			}
		}
		while (LL_I2S_IsActiveFlag_BSY(i2sHandle)) {
			if (System::GetTick() - tickStart > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}


	void ErrorInterrupt() {
		if (!LL_I2S_IsEnabledIT_ERR(i2sHandle)) {
			return;
		}

		if (LL_I2S_IsActiveFlag_UDR(i2sHandle)) {
			LL_I2S_ClearFlag_UDR(i2sHandle);
			CallError(Error::Underrun);
		}
		if (LL_I2S_IsActiveFlag_OVR(i2sHandle)) {
			LL_I2S_ClearFlag_OVR(i2sHandle);
			CallError(Error::Overrun);
		}
		if (LL_I2S_IsActiveFlag_FRE(i2sHandle)) {
			LL_I2S_ClearFlag_FRE(i2sHandle);
			CallError(Error::FrameError);
		}
	}


	void TransmitInterrupt() {
		if (!LL_I2S_IsEnabledIT_TXE(i2sHandle) || !LL_I2S_IsActiveFlag_TXE(i2sHandle)) {
			return;
		}

		// Every word is already written; the last one is in the shift register. Completion waits until the bit clock is
		// idle, so a consumer that stops or reconfigures in TxComplete cannot cut the final frame. TXE stays high while
		// the block is empty, so this interrupt fires again until BSY clears.
		if (txPosition >= txLength) {
			if (LL_I2S_IsActiveFlag_BSY(i2sHandle)) {
				return;
			}

			LL_I2S_DisableIT_TXE(i2sHandle);
			LL_I2S_DisableIT_ERR(i2sHandle);
			txState = ResultStatus::ready;
			CallInterrupt(Irq::TxComplete);
			return;
		}

		LL_I2S_TransmitData16(i2sHandle, txBuffer[txPosition]);
		txPosition = txPosition + 1;

		// The half-way notice lets the consumer refill the half that has just been sent while the other one plays.
		if (txPosition == txLength / 2) {
			CallInterrupt(Irq::TxHalfComplete);
		}

		if (txPosition < txLength) {
			return;
		}

		// Non-circular: the last word is written, and completion is handled on the next entry once BSY clears.
		// Circular: TxComplete fires when the last buffer word is written, while that word still shifts out. The
		// callback must not stop or reconfigure the block.
		if (txCircular) {
			txPosition = 0;
			CallInterrupt(Irq::TxComplete);
		}
	}


	void ReceiveInterrupt() {
		if (!LL_I2S_IsEnabledIT_RXNE(i2sHandle) || !LL_I2S_IsActiveFlag_RXNE(i2sHandle)) {
			return;
		}

		rxBuffer[rxPosition] = LL_I2S_ReceiveData16(i2sHandle);
		rxPosition = rxPosition + 1;

		if (rxPosition == rxLength / 2) {
			CallInterrupt(Irq::RxHalfComplete);
		}

		if (rxPosition < rxLength) {
			return;
		}

		if (rxCircular) {
			rxPosition = 0;
			CallInterrupt(Irq::RxComplete);
			return;
		}

		LL_I2S_DisableIT_RXNE(i2sHandle);
		LL_I2S_DisableIT_ERR(i2sHandle);
		rxState = ResultStatus::ready;
		CallInterrupt(Irq::RxComplete);
	}


	ResultStatus ValidateParameters() const {
		if (parameters.standard == Standard::PDM) {
			return ResultStatus::notSupported;
		}
		if (parameters.direction == Direction::TxRx) {
			return ResultStatus::notSupported;
		}
		if (parameters.slotCount.Get() != 0 && parameters.slotCount.Get() != SlotCount::S2.Get()) {
			return ResultStatus::notSupported;
		}
		if (parameters.activeSlots.Get() != 0 && parameters.activeSlots.Get() != Slot::Stereo.Get()) {
			return ResultStatus::notSupported;
		}
		if (parameters.sampleRateHz == 0) {
			return ResultStatus::invalidParameter;
		}
		if (!(IS_I2S_ALL_INSTANCE(i2sHandle))) {
			return ResultStatus::notSupported;
		}
		return ResultStatus::ok;
	}


	// Finds the I2SDIV/ODD pair that gives the requested rate. The divider is 2*I2SDIV+ODD and I2SDIV must be 2..255, so
	// the divider is 4..511. The arithmetic follows the ST HAL: the bit clock frame is 2 channels for the I2S standards
	// and 1 channel for PCM, and with MCK the MCK is 8 (16-bit channels) or 4 (32-bit channels) times that bit clock.
	ResultStatus CalculateDivider(uint32& divider) const {
		uint64 channelBits = (parameters.dataFormat == DataFormat::B16) ? 16 : 32;
		uint64 frameBits = channelBits;
		if (parameters.standard != Standard::PCMShort && parameters.standard != Standard::PCMLong) {
			frameBits = channelBits * 2;
		}

		uint64 denominator = frameBits * parameters.sampleRateHz;
		if (parameters.mclkOutput) {
			denominator *= (channelBits == 16) ? 8 : 4;
		}

		// Rounds to the nearest divider, so the rate error stays within half a divider step.
		uint64 rounded = (static_cast<uint64>(inputBusClockHz) * 2 + denominator) / (denominator * 2);
		if (rounded < 4 || rounded > 511) {
			return ResultStatus::invalidParameter;
		}

		divider = static_cast<uint32>(rounded);
		return ResultStatus::ok;
	}


	// Words per frame on the wire: two channels, one DR access per channel for 16-bit data and two for 24/32-bit data.
	uint32 FrameWords() const {
		if (parameters.dataFormat == DataFormat::B24 || parameters.dataFormat == DataFormat::B32) {
			return 4;
		}
		return 2;
	}


	bool IsPeripheralClockEnabled() const {
		if (i2sHandle == SPI2) {
			return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_SPI2) != 0;
		}

		if (i2sHandle == SPI3) {
			return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_SPI3) != 0;
		}

		return false;
	}


	uint32 CastMode() const {
		if (parameters.mode == Mode::Master) {
			return (parameters.direction == Direction::Tx) ? LL_I2S_MODE_MASTER_TX : LL_I2S_MODE_MASTER_RX;
		}
		return (parameters.direction == Direction::Tx) ? LL_I2S_MODE_SLAVE_TX : LL_I2S_MODE_SLAVE_RX;
	}


	uint32 CastStandard() const {
		switch (parameters.standard) {
			case Standard::Philips: return LL_I2S_STANDARD_PHILIPS;
			case Standard::MSB: return LL_I2S_STANDARD_MSB;
			case Standard::LSB: return LL_I2S_STANDARD_LSB;
			case Standard::PCMShort: return LL_I2S_STANDARD_PCM_SHORT;
			case Standard::PCMLong: return LL_I2S_STANDARD_PCM_LONG;
			case Standard::PDM: break;
		}
		SystemAbort();
		return 0;
	}


	uint32 CastDataFormat() const {
		switch (parameters.dataFormat) {
			case DataFormat::B16: return LL_I2S_DATAFORMAT_16B;
			case DataFormat::B16Extended: return LL_I2S_DATAFORMAT_16B_EXTENDED;
			case DataFormat::B24: return LL_I2S_DATAFORMAT_24B;
			case DataFormat::B32: return LL_I2S_DATAFORMAT_32B;
		}
		SystemAbort();
		return 0;
	}


	uint32 CastClockPolarity() const {
		switch (parameters.clockPolarity) {
			case ClockPolarity::High: return LL_I2S_POLARITY_HIGH;
			case ClockPolarity::Low: return LL_I2S_POLARITY_LOW;
		}
		SystemAbort();
		return 0;
	}
};


#endif
