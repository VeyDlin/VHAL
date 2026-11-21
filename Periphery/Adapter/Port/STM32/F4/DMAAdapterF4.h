#pragma once
#include <Adapter/DMAAdapter.h>


// STM32F4 DMA port: DMA1 and DMA2, streams 0..7.
//
// Stream numbering: the constructor argument is the stream index 0..7, passed
// to the LL API unchanged. This differs from the G0/G4 ports, which use 1-based
// channels, because the F4 LL API numbers streams from 0.
//
// Request selection: DMAPeripheralOption is the CHSEL field, Peripheral::Channel0..Channel7.
// Which peripheral request is wired to which channel depends on the stream
// (see the DMA request mapping tables of RM0090 for DMA1 and DMA2), so the caller takes the value from there.
//
// Modes:
//  - FIFO is used automatically when the transfer needs it: memory-to-memory,
//    PSIZE != MSIZE, or any burst. SetFifoEnabled() forces it on for other cases.
//  - Memory-to-memory is only available on DMA2 and cannot run in circular mode.
//  - Address modes are applied per side: the peripheral side is the source for
//    peripheral-to-memory and memory-to-memory, and the destination for memory-to-peripheral.
//  - NDTR counts peripheral data items, so the transfer size is divided by the
//    peripheral width and lastTransferSize is reported in those items.
//  - The half-transfer interrupt is enabled for a run only when onHalfTransfer is
//    set at StartTransfer time.
//  - Stream configuration is only written while the stream is disabled. If the stream
//    is still enabled, Initialization() and StartTransfer() return busy and do not touch it;
//    Stop() is the only call that aborts a run, and the caller retries once the stream has stopped.
//  - Priority must be 0..3 (low, medium, high, very high); anything else is invalidParameter.
//  - Interrupt flags are read and cleared per stream: stream 0..3 use LISR/LIFCR,
//    stream 4..7 use HISR/HIFCR, and each stream has its own flag bit group.
//  - GetStatus() reports ok when a circular stream is enabled, because circular mode never completes.
//    It reports stop when a normal-mode stream was stopped before it completed.
//
// BSP must:
//  - Enable the DMA clock (RCC_AHB1ENR DMA1EN or DMA2EN) before the adapter is used.
//    The adapter never configures the RCC tree; Initialization() returns notReady while the clock is off.
//  - Enable the NVIC line of each stream the adapter uses and forward it to IrqHandler()
//    of the adapter that owns that stream: DMA1_Stream0_IRQn..DMA1_Stream7_IRQn and
//    DMA2_Stream0_IRQn..DMA2_Stream7_IRQn. Every F4 stream has its own vector, so one
//    adapter instance maps to exactly one vector.
//  - Error flags (TEIF, DMEIF, FEIF) are delivered on the same vector and reported through onError.


using ADMA = class DMAAdapterF4;


class DMAAdapterF4: public DMAAdapter<DMA_TypeDef> {
public:
	struct Peripheral {
		static inline constexpr DMAPeripheralOption Channel0 { LL_DMA_CHANNEL_0 };
		static inline constexpr DMAPeripheralOption Channel1 { LL_DMA_CHANNEL_1 };
		static inline constexpr DMAPeripheralOption Channel2 { LL_DMA_CHANNEL_2 };
		static inline constexpr DMAPeripheralOption Channel3 { LL_DMA_CHANNEL_3 };
		static inline constexpr DMAPeripheralOption Channel4 { LL_DMA_CHANNEL_4 };
		static inline constexpr DMAPeripheralOption Channel5 { LL_DMA_CHANNEL_5 };
		static inline constexpr DMAPeripheralOption Channel6 { LL_DMA_CHANNEL_6 };
		static inline constexpr DMAPeripheralOption Channel7 { LL_DMA_CHANNEL_7 };
	};

	struct FifoThresholdOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct MemoryBurstOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct PeripheralBurstOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct FifoThreshold {
		static inline constexpr FifoThresholdOption Quarter       { LL_DMA_FIFOTHRESHOLD_1_4 };
		static inline constexpr FifoThresholdOption Half          { LL_DMA_FIFOTHRESHOLD_1_2 };
		static inline constexpr FifoThresholdOption ThreeQuarters { LL_DMA_FIFOTHRESHOLD_3_4 };
		static inline constexpr FifoThresholdOption Full          { LL_DMA_FIFOTHRESHOLD_FULL };
	};

	// MBURST and PBURST are separate fields in SxCR with different bit positions,
	// so each side needs its own constant set.
	struct MemoryBurst {
		static inline constexpr MemoryBurstOption Single { LL_DMA_MBURST_SINGLE };
		static inline constexpr MemoryBurstOption Inc4   { LL_DMA_MBURST_INC4 };
		static inline constexpr MemoryBurstOption Inc8   { LL_DMA_MBURST_INC8 };
		static inline constexpr MemoryBurstOption Inc16  { LL_DMA_MBURST_INC16 };
	};

	struct PeripheralBurst {
		static inline constexpr PeripheralBurstOption Single { LL_DMA_PBURST_SINGLE };
		static inline constexpr PeripheralBurstOption Inc4   { LL_DMA_PBURST_INC4 };
		static inline constexpr PeripheralBurstOption Inc8   { LL_DMA_PBURST_INC8 };
		static inline constexpr PeripheralBurstOption Inc16  { LL_DMA_PBURST_INC16 };
	};


private:
	// Bit offset of each flag inside its LISR/HISR group of four streams (DMA_LISR/DMA_HISR register).
	enum class StreamFlag : uint8 {
		FifoError        = 0,
		DirectModeError  = 2,
		TransferError    = 3,
		HalfTransfer     = 4,
		TransferComplete = 5
	};

	// Group bit offset for streams 0..3 (and 4..7 by modulo) in LISR/HISR.
	static inline constexpr uint32 StreamGroupOffsets[4] { 0, 6, 16, 22 };
	bool fifoEnabled = false;
	FifoThresholdOption fifoThreshold { FifoThreshold::Full };
	MemoryBurstOption memoryBurst { MemoryBurst::Single };
	PeripheralBurstOption peripheralBurst { PeripheralBurst::Single };

	// Latched by IrqHandler so GetStatus still reports the outcome after the ISR has cleared the flags.
	bool transferComplete = false;
	bool transferError = false;

	// Set by StartTransfer, cleared by Stop(), completion and error. IrqHandler uses it to
	// drop events of a stopped stream: Stop() leaves EN set while the stream drains, and the interrupts stay enabled.
	bool running = false;


public:
	DMAAdapterF4(): DMAAdapter(nullptr, 8) { }
	DMAAdapterF4(DMA_TypeDef *dma, uint32 channel): DMAAdapter(dma, channel) { }


	virtual ResultStatus Initialization() override {
		if (!IsStreamValid()) {
			return ResultStatus::invalidParameter;
		}

		if (parameters.priority > 3) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		// The clock belongs to the BSP; the adapter only verifies it is on.
		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		// Memory-to-memory exists only on DMA2, and circular mode is not allowed for it.
		if (parameters.direction == Direction::MemoryToMemory) {
			if (!IsDma2() || parameters.circularMode) {
				return ResultStatus::notSupported;
			}
		}

		// SxCR and SxFCR are write-protected while EN is set. Return busy and leave the stream alone:
		// disabling a running stream would abort it without a TC or TE flag. The caller retries after Stop().
		if (LL_DMA_IsEnabledStream(dmaHandle, dmaChannel)) {
			return ResultStatus::busy;
		}

		LL_DMA_SetChannelSelection(dmaHandle, dmaChannel, parameters.peripheral.Get());
		LL_DMA_SetDataTransferDirection(dmaHandle, dmaChannel, CastDirection());
		LL_DMA_SetStreamPriorityLevel(dmaHandle, dmaChannel, CastPriority());
		LL_DMA_SetMode(dmaHandle, dmaChannel, parameters.circularMode ? LL_DMA_MODE_CIRCULAR : LL_DMA_MODE_NORMAL);
		LL_DMA_SetPeriphIncMode(dmaHandle, dmaChannel, CastPeriphIncMode());
		LL_DMA_SetMemoryIncMode(dmaHandle, dmaChannel, CastMemoryIncMode());
		LL_DMA_SetPeriphSize(dmaHandle, dmaChannel, CastPeriphDataWidth());
		LL_DMA_SetMemorySize(dmaHandle, dmaChannel, CastMemoryDataWidth());
		LL_DMA_SetMemoryBurstxfer(dmaHandle, dmaChannel, memoryBurst.Get());
		LL_DMA_SetPeriphBurstxfer(dmaHandle, dmaChannel, peripheralBurst.Get());

		// Direct mode cannot carry bursts or mixed sizes, so FIFO is forced in those cases.
		if (UseFifo()) {
			LL_DMA_EnableFifoMode(dmaHandle, dmaChannel);
			LL_DMA_SetFIFOThreshold(dmaHandle, dmaChannel, fifoThreshold.Get());
		} else {
			LL_DMA_DisableFifoMode(dmaHandle, dmaChannel);
		}

		return AfterInitialization();
	}


	virtual ResultStatus StartTransfer(const uint8* from, uint8* to, uint32 size) override {
		if (!IsStreamValid()) {
			return ResultStatus::invalidParameter;
		}

		// Initialization() checks the clock and the stream state and writes every pending
		// setter value, so a value stored while a run was active is applied before this transfer.
		ResultStatus status = Initialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		uint32 itemBytes = PeripheralDataWidthBytes();
		if (size == 0 || size % itemBytes != 0 || size / itemBytes > 0xFFFFu) {
			return ResultStatus::invalidParameter;
		}
		uint32 count = size / itemBytes;

		// Stale flags from the previous run would fire as soon as the stream is enabled.
		ClearAllStreamFlags();

		if (parameters.direction == Direction::MemoryToPeripheral) {
			LL_DMA_SetMemoryAddress(dmaHandle, dmaChannel, AddressValue(from));
			LL_DMA_SetPeriphAddress(dmaHandle, dmaChannel, AddressValue(to));
		} else {
			LL_DMA_SetPeriphAddress(dmaHandle, dmaChannel, AddressValue(from));
			LL_DMA_SetMemoryAddress(dmaHandle, dmaChannel, AddressValue(to));
		}

		LL_DMA_SetDataLength(dmaHandle, dmaChannel, count);

		// Transfer and direct-mode errors are always on: they stop the stream in hardware.
		LL_DMA_EnableIT_TE(dmaHandle, dmaChannel);
		LL_DMA_EnableIT_DME(dmaHandle, dmaChannel);

		// FIFO errors can only occur in FIFO mode.
		if (UseFifo()) {
			LL_DMA_EnableIT_FE(dmaHandle, dmaChannel);
		} else {
			LL_DMA_DisableIT_FE(dmaHandle, dmaChannel);
		}

		if (parameters.enableTransferCompleteIT) {
			LL_DMA_EnableIT_TC(dmaHandle, dmaChannel);
		} else {
			LL_DMA_DisableIT_TC(dmaHandle, dmaChannel);
		}

		if (onHalfTransfer) {
			LL_DMA_EnableIT_HT(dmaHandle, dmaChannel);
		} else {
			LL_DMA_DisableIT_HT(dmaHandle, dmaChannel);
		}

		transferComplete = false;
		transferError = false;
		lastTransferSize = count;

		running = true;
		LL_DMA_EnableStream(dmaHandle, dmaChannel);
		return ResultStatus::ok;
	}


	// The only call that aborts a run. The stream may keep EN set for a short time while it drains;
	// the callbacks of that drain are suppressed by the running latch in IrqHandler().
	virtual ResultStatus Stop() override {
		if (!IsStreamValid()) {
			return ResultStatus::invalidParameter;
		}

		running = false;
		LL_DMA_DisableStream(dmaHandle, dmaChannel);
		return ResultStatus::ok;
	}


	virtual ResultStatus GetStatus() override {
		if (!IsStreamValid()) {
			return ResultStatus::invalidParameter;
		}

		if (transferError || IsStreamFlagActive(StreamFlag::TransferError) || IsStreamFlagActive(StreamFlag::DirectModeError) || IsStreamFlagActive(StreamFlag::FifoError)) {
			return ResultStatus::error;
		}

		// Circular mode never completes, so the completion latch says nothing about the run.
		// ok means the stream is still cycling; stop means it has been stopped.
		if (IsCircular()) {
			if (LL_DMA_IsEnabledStream(dmaHandle, dmaChannel)) {
				return ResultStatus::ok;
			}
			return ResultStatus::stop;
		}

		if (transferComplete || IsStreamFlagActive(StreamFlag::TransferComplete)) {
			return ResultStatus::ok;
		}

		// Stopped before completion: the run is over and will not complete.
		if (!LL_DMA_IsEnabledStream(dmaHandle, dmaChannel)) {
			return ResultStatus::stop;
		}

		return ResultStatus::busy;
	}


	virtual inline void IrqHandler() override {
		if (!IsStreamValid()) {
			return;
		}

		// A stopped stream still raises its enabled interrupts while it drains.
		// Clear the flags and report nothing.
		if (!running) {
			ClearAllStreamFlags();
			return;
		}

		// An error ends the run. TE and DME stop the stream in hardware; FE may not, so the stream is stopped explicitly.
		// LL_DMA_DisableStream is harmless when the stream has already stopped.
		if (IsErrorPending()) {
			LL_DMA_DisableStream(dmaHandle, dmaChannel);
			LL_DMA_DisableIT_TE(dmaHandle, dmaChannel);
			LL_DMA_DisableIT_DME(dmaHandle, dmaChannel);
			LL_DMA_DisableIT_FE(dmaHandle, dmaChannel);
			LL_DMA_DisableIT_TC(dmaHandle, dmaChannel);
			LL_DMA_DisableIT_HT(dmaHandle, dmaChannel);

			ClearAllStreamFlags();
			running = false;
			transferError = true;
			CallError();
			return;
		}

		if (IsStreamFlagActive(StreamFlag::HalfTransfer) && LL_DMA_IsEnabledIT_HT(dmaHandle, dmaChannel)) {
			// Normal mode raises half-transfer once; circular mode keeps it for every half.
			if (!IsCircular()) {
				LL_DMA_DisableIT_HT(dmaHandle, dmaChannel);
			}

			ClearStreamFlag(StreamFlag::HalfTransfer);
			CallHalfTransfer();
		}

		if (IsStreamFlagActive(StreamFlag::TransferComplete) && LL_DMA_IsEnabledIT_TC(dmaHandle, dmaChannel)) {
			// In normal mode the stream has stopped, so the run ends here and the other interrupts of this run are switched off.
			if (!IsCircular()) {
				running = false;
				LL_DMA_DisableIT_TC(dmaHandle, dmaChannel);
				LL_DMA_DisableIT_HT(dmaHandle, dmaChannel);
				LL_DMA_DisableIT_TE(dmaHandle, dmaChannel);
				LL_DMA_DisableIT_DME(dmaHandle, dmaChannel);
				LL_DMA_DisableIT_FE(dmaHandle, dmaChannel);
			}

			ClearStreamFlag(StreamFlag::TransferComplete);
			transferComplete = true;
			CallTransferComplete();
		}
	}


	// True when this adapter is the given stream of the given controller. A port uses it to check that a peripheral request
	// is on the stream the chip wires it to, since the request table is per stream.
	bool IsStream(DMA_TypeDef *controller, uint32 stream) const {
		return dmaHandle == controller && dmaChannel == stream;
	}


	// Stores the value and applies it through Initialization(). While a run is active the call returns
	// busy and the value stays pending; the next Initialization() or Start() writes it.
	ResultStatus SetFifoEnabled(bool enabled) {
		fifoEnabled = enabled;
		return Initialization();
	}


	ResultStatus SetFifoThreshold(FifoThresholdOption threshold) {
		fifoThreshold = threshold;
		return Initialization();
	}


	// Burst lengths must fit the FIFO threshold and the data size (see the burst and FIFO
	// table in the RM0090 DMA chapter); the adapter does not validate that combination.
	ResultStatus SetMemoryBurst(MemoryBurstOption burst) {
		memoryBurst = burst;
		return Initialization();
	}


	ResultStatus SetPeripheralBurst(PeripheralBurstOption burst) {
		peripheralBurst = burst;
		return Initialization();
	}


private:
	// The register takes a 32-bit address. The cast goes through uintptr_t, so a 64-bit host build of the port compiles.
	static uint32 AddressValue(const void *address) {
		return static_cast<uint32>(reinterpret_cast<uintptr_t>(address));
	}


	bool IsStreamValid() const {
		return dmaHandle != nullptr && dmaChannel < 8;
	}


	// DMA2 is the only instance that supports memory-to-memory. Not every F4 has DMA2.
	bool IsDma2() const {
		#if defined(DMA2)
			return dmaHandle == DMA2;
		#else
			return false;
		#endif
	}


	bool IsClockEnabled() const {
		if (IsDma2()) {
			return LL_AHB1_GRP1_IsEnabledClock(LL_AHB1_GRP1_PERIPH_DMA2) != 0;
		}
		return LL_AHB1_GRP1_IsEnabledClock(LL_AHB1_GRP1_PERIPH_DMA1) != 0;
	}


	// Masks the CIRC bit only; PFCTRL is never set by this port.
	bool IsCircular() const {
		return (LL_DMA_GetMode(dmaHandle, dmaChannel) & LL_DMA_MODE_CIRCULAR) != 0;
	}


	bool IsErrorPending() const {
		return (IsStreamFlagActive(StreamFlag::TransferError) && LL_DMA_IsEnabledIT_TE(dmaHandle, dmaChannel))
			|| (IsStreamFlagActive(StreamFlag::DirectModeError) && LL_DMA_IsEnabledIT_DME(dmaHandle, dmaChannel))
			|| (IsStreamFlagActive(StreamFlag::FifoError) && LL_DMA_IsEnabledIT_FE(dmaHandle, dmaChannel));
	}


	// FIFO is needed when the direct mode cannot express the transfer.
	bool RequiresFifo() const {
		if (parameters.direction == Direction::MemoryToMemory) {
			return true;
		}

		if (PeripheralDataWidth() != parameters.dataWidth) {
			return true;
		}

		return memoryBurst.Get() != LL_DMA_MBURST_SINGLE || peripheralBurst.Get() != LL_DMA_PBURST_SINGLE;
	}


	bool UseFifo() const {
		return fifoEnabled || RequiresFifo();
	}


	uint32 StreamFlagBit(StreamFlag flag) const {
		return StreamGroupOffsets[dmaChannel % 4] + static_cast<uint32>(flag);
	}


	uint32 StreamFlagMask(StreamFlag flag) const {
		return static_cast<uint32>(1) << StreamFlagBit(flag);
	}


	bool IsStreamFlagActive(StreamFlag flag) const {
		uint32 status = (dmaChannel < 4) ? dmaHandle->LISR : dmaHandle->HISR;
		return (status & StreamFlagMask(flag)) != 0;
	}


	void ClearStreamFlag(StreamFlag flag) {
		uint32 mask = StreamFlagMask(flag);
		if (dmaChannel < 4) {
			dmaHandle->LIFCR = mask;
		} else {
			dmaHandle->HIFCR = mask;
		}
	}


	void ClearAllStreamFlags() {
		uint32 mask = StreamFlagMask(StreamFlag::FifoError)
			| StreamFlagMask(StreamFlag::DirectModeError)
			| StreamFlagMask(StreamFlag::TransferError)
			| StreamFlagMask(StreamFlag::HalfTransfer)
			| StreamFlagMask(StreamFlag::TransferComplete);

		if (dmaChannel < 4) {
			dmaHandle->LIFCR = mask;
		} else {
			dmaHandle->HIFCR = mask;
		}
	}


	DataWidth PeripheralDataWidth() const {
		if (parameters.periphDataWidth != static_cast<DataWidth>(0)) {
			return parameters.periphDataWidth;
		}
		return parameters.dataWidth;
	}


	uint32 PeripheralDataWidthBytes() const {
		return static_cast<uint32>(PeripheralDataWidth()) / 8;
	}


	// The peripheral is the source for peripheral-to-memory and memory-to-memory, and the destination
	// for memory-to-peripheral. Each side is derived from the requested address mode and that role,
	// so the mode is applied to the side it names.
	bool PeripheralIsSource() const {
		return parameters.direction != Direction::MemoryToPeripheral;
	}


	bool SourceIncrements() const {
		return parameters.addressMode == AddressMode::IncrementingToFixed
			|| parameters.addressMode == AddressMode::IncrementingToIncrementing;
	}


	bool DestinationIncrements() const {
		return parameters.addressMode == AddressMode::FixedToIncrementing
			|| parameters.addressMode == AddressMode::IncrementingToIncrementing;
	}


	uint32 CastDirection() const {
		switch (parameters.direction) {
			case Direction::MemoryToPeripheral: return LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
			case Direction::PeripheralToMemory: return LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
			case Direction::MemoryToMemory:     return LL_DMA_DIRECTION_MEMORY_TO_MEMORY;
		}
		SystemAbort();
		return 0;
	}


	// Priority is range-checked in Initialization(), so the switch only sees 0..3.
	uint32 CastPriority() const {
		switch (parameters.priority) {
			case 0: return LL_DMA_PRIORITY_LOW;
			case 1: return LL_DMA_PRIORITY_MEDIUM;
			case 2: return LL_DMA_PRIORITY_HIGH;
			case 3: return LL_DMA_PRIORITY_VERYHIGH;
		}
		SystemAbort();
		return 0;
	}


	uint32 CastPeriphDataWidth() const {
		switch (PeripheralDataWidth()) {
			case DataWidth::B8:  return LL_DMA_PDATAALIGN_BYTE;
			case DataWidth::B16: return LL_DMA_PDATAALIGN_HALFWORD;
			case DataWidth::B32: return LL_DMA_PDATAALIGN_WORD;
		}
		SystemAbort();
		return 0;
	}


	uint32 CastMemoryDataWidth() const {
		switch (parameters.dataWidth) {
			case DataWidth::B8:  return LL_DMA_MDATAALIGN_BYTE;
			case DataWidth::B16: return LL_DMA_MDATAALIGN_HALFWORD;
			case DataWidth::B32: return LL_DMA_MDATAALIGN_WORD;
		}
		SystemAbort();
		return 0;
	}


	uint32 CastPeriphIncMode() const {
		bool increments = PeripheralIsSource() ? SourceIncrements() : DestinationIncrements();
		return increments ? LL_DMA_PERIPH_INCREMENT : LL_DMA_PERIPH_NOINCREMENT;
	}


	uint32 CastMemoryIncMode() const {
		bool increments = PeripheralIsSource() ? DestinationIncrements() : SourceIncrements();
		return increments ? LL_DMA_MEMORY_INCREMENT : LL_DMA_MEMORY_NOINCREMENT;
	}
};
