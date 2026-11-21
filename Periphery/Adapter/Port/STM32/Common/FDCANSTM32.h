#pragma once
#include <array>


// Shared STM32 FDCAN code for G4 and G0: classic CAN and CAN FD on the FDCAN register block. A family file derives from
// FDCANAdapterSTM32 and supplies the bus clock check. Everything here compiles to nothing where the device header has no
// FDCAN1 (STM32G071).
//
// Message RAM: ST's FDCAN does not have the configurable layout of the generic Bosch M_CAN, so RXF0C, RXBC, SIDFC and the
// other layout registers do not exist. Each instance owns a fixed block at SRAMCAN_BASE + instance x 0x350 (FDCAN2 and
// FDCAN3 exist on some G4 chips only):
//   0x000  28 standard filter elements, 4 bytes each
//   0x070  8 extended filter elements, 8 bytes each
//   0x0B0  Rx FIFO 0, 3 elements of 72 bytes
//   0x188  Rx FIFO 1, 3 elements of 72 bytes (not used by this port)
//   0x260  Tx event FIFO, 3 elements of 8 bytes
//   0x278  Tx FIFO, 3 elements of 72 bytes
// Every element has room for 64 data bytes. The offsets are the constants of the HAL (stm32g4xx_hal_fdcan.c; the G0 copy
// is identical). The message RAM is read and written as 32-bit words only.
//
// What the BSP must do (the adapter never touches RCC or NVIC):
//   - Clock enable: set FDCANEN in the bus clock enable register (RCC_APB1ENR1 on G4, RCC_APBENR1 on G0). The bit is
//     shared by all instances of the chip. Initialization() returns notReady while it is off.
//   - Kernel clock: select the FDCAN kernel clock with RCC_CCIPR FDCANSEL. The bit timing is computed from the frequency
//     given as kernelClockHz, so pass the clock the FDCAN kernel really receives.
//   - Clock divider: FDCAN_CONFIG->CKDIV is one register shared by all instances. The adapter does not write it. The BSP
//     sets it before the first SetParameters(), and kernelClockHz must already be the frequency after the divider.
//   - Pins: put TX and RX in the FDCAN alternate function given by the datasheet.
//   - NVIC: enable the two interrupt lines of the instance, FDCANx_IT0_IRQn and FDCANx_IT1_IRQn, and call IrqHandler() from
//     each of them. The adapter serves every source on either line, so both calls are needed only to use both vectors. On
//     G0 the lines are shared with timers (TIM16_FDCAN_IT0_IRQn, TIM17_FDCAN_IT1_IRQn): the handler calls IrqHandler() and
//     the timer code.
//   - onReceive may be assigned at any time. The Rx FIFO 0 new-message interrupt is on while the controller is started,
//     so a handler assigned later is served from the next frame. Frames already in FIFO 0 stay there for Receive() until
//     a new frame arrives.
//
// Timestamps: Frame::timestamp is the 16-bit timestamp counter, which counts CAN bit times (TSS = 01, TCP = 0) and wraps
// every 65536 bit times.
//
// Transmit failures: with automatic retransmission off, FDCAN drops a frame that lost arbitration or met an error, and
// it writes no event for it. Send() stores a serial number in the message marker of each Tx element and sets EFC, so the
// hardware stores a Tx event for every transmitted frame; the Tx event FIFO returns that marker. A gap in the serial
// numbers is therefore a failed frame. An empty Tx FIFO settles the frames that are still open, so a failure is reported
// at the latest at the Tx FIFO empty interrupt. This assumes that a Tx FIFO element is released only when its frame has
// ended on the bus, which is not verified on hardware (the reference manual is not on the build machine).
//
// Transmitter delay compensation (TDC) is enabled for data bit rates of 1 Mbit/s and above. TDCO is the data sample point
// in minimum time quanta, TDCF is 0. TDC stays off when that offset would exceed 127.
#if defined(FDCAN1)
#include <Adapter/CANAdapter.h>


class FDCANAdapterSTM32: public CANAdapter<FDCAN_GlobalTypeDef> {
private:
	struct BitTiming {
		uint32 prescaler = 0;
		// TSEG1 (propagation plus phase 1) and TSEG2 (phase 2), in time quanta. The sync segment is one quantum.
		uint32 tseg1 = 0;
		uint32 tseg2 = 0;
		uint32 sjw = 0;
	};


	struct TimingLimits {
		uint32 maxPrescaler;
		uint32 maxTseg1;
		uint32 maxTseg2;
		uint32 maxSjw;
		uint32 minQuanta;
	};


	static inline constexpr TimingLimits NominalLimits = { 512, 256, 128, 4, 8 };
	static inline constexpr TimingLimits DataLimits = { 32, 32, 16, 4, 5 };
	static inline constexpr uint32 MaxQuanta = 25;
	static inline constexpr uint32 MaxNominalBitrate = 1000000;
	static inline constexpr uint32 MaxDataBitrate = 8000000;
	static inline constexpr uint64 MaxBitrateErrorPpm = 5000;
	static inline constexpr uint64 MaxSamplePointErrorPermille = 20;
	static inline constexpr uint32 TdcMinDataBitrate = 1000000;
	static inline constexpr uint32 TdcMaxOffset = 127;
	static inline constexpr uint64 TimeoutMs = 10;
	static inline constexpr uint32 StandardFilterCount = 28;
	static inline constexpr uint32 ExtendedFilterCount = 8;
	static inline constexpr uint32 FifoElementCount = 3;
	static inline constexpr uint32 StandardFilterOffset = 0x000;
	static inline constexpr uint32 ExtendedFilterOffset = 0x070;
	static inline constexpr uint32 RxFifo0Offset = 0x0B0;
	static inline constexpr uint32 RxFifo1Offset = 0x188;
	static inline constexpr uint32 TxEventOffset = 0x260;
	static inline constexpr uint32 TxFifoOffset = 0x278;
	static inline constexpr uint32 MessageRamInstanceSize = 0x350;
	static inline constexpr uint32 StandardFilterSize = 4;
	static inline constexpr uint32 ExtendedFilterSize = 8;
	static inline constexpr uint32 RxElementSize = 72;
	static inline constexpr uint32 TxEventSize = 8;
	static inline constexpr uint32 TxElementSize = 72;
	static inline constexpr uint32 ElementHeaderSize = 8;
	static inline constexpr uint32 StandardIdMask = 0x7FF;
	static inline constexpr uint32 ExtendedIdMask = 0x1FFFFFFF;
	static inline constexpr uint32 ElementExtended = 1U << 30;
	static inline constexpr uint32 ElementRemote = 1U << 29;
	static inline constexpr uint32 ElementStandardShift = 18;
	static inline constexpr uint32 ElementFdFormat = 1U << 21;
	static inline constexpr uint32 ElementBitRateSwitch = 1U << 20;
	static inline constexpr uint32 ElementStoreEvent = 1U << 23;
	static inline constexpr uint32 ElementDlcShift = 16;
	static inline constexpr uint32 ElementMarkerShift = 24;
	static inline constexpr uint32 FilterClassic = 2;
	static inline constexpr uint32 FilterStoreRxFifo0 = 1;
	static inline constexpr uint32 NonMatchingReject = 2;
	static inline constexpr uint32 TimestampPerBitTime = 1;
	static inline constexpr uint32 TxBufferAll = 0x7;
	static inline constexpr uint32 TxFifoEmptyLevel = 3;
	static inline constexpr uint32 ErrorCodeNone = 0;
	static inline constexpr uint32 ErrorCodeNoChange = 7;
	static inline constexpr uint32 CccrConfigMask = FDCAN_CCCR_DAR | FDCAN_CCCR_MON | FDCAN_CCCR_TEST | FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_ASM | FDCAN_CCCR_NISO;
	static inline constexpr uint32 RxgfcMask = FDCAN_RXGFC_LSS | FDCAN_RXGFC_LSE | FDCAN_RXGFC_F0OM | FDCAN_RXGFC_ANFS | FDCAN_RXGFC_ANFE | FDCAN_RXGFC_RRFS | FDCAN_RXGFC_RRFE;
	static inline constexpr uint8 DlcLengths[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };
	// Written in thread context and read by the interrupt, so both are volatile: the store in Stop() must not move past the
	// register write that cancels the frames.
	volatile bool initialized = false;
	volatile bool started = false;
	bool filterRamReady = false;
	BusState lastBusState = BusState::Stopped;
	// Serial number of the last frame placed in the Tx FIFO, and of the last frame that has been resolved (sent or failed).
	// Both wrap at 8 bits, like the message marker that carries the serial.
	uint8 txPlaced = 0;
	uint8 txResolved = 0;


public:
	FDCANAdapterSTM32() = default;


	FDCANAdapterSTM32(FDCAN_GlobalTypeDef *can, uint32 kernelClockHz): CANAdapter(can, kernelClockHz) {
	}


	virtual ResultStatus Start() override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!initialized) {
			return ResultStatus::noInit;
		}

		if (IsStarted()) {
			return ResultStatus::ok;
		}

		// Interrupts are enabled here rather than in Initialization(). The new-message interrupt is on for as long as the
		// controller runs, so an onReceive assigned at any time is served. Without onReceive, ServiceReceive() clears the
		// flag and leaves the frames for Receive(). The flag is an event, so the interrupt cannot re-enter by itself.
		uint32 interrupts = FDCAN_IE_RF0NE | FDCAN_IE_RF0LE | FDCAN_IE_TEFNE | FDCAN_IE_TFEE | FDCAN_IE_EWE | FDCAN_IE_EPE | FDCAN_IE_BOE | FDCAN_IE_PEAE | FDCAN_IE_PEDE;

		canHandle->IE = interrupts;
		started = true;
		status = LeaveInitMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		UpdateBusState();
		return ResultStatus::ok;
	}


	virtual ResultStatus Stop() override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!started) {
			return ResultStatus::ok;
		}

		// Cleared first: a bus-off recovery that is raised meanwhile must not restart the controller.
		started = false;

		// Every queued frame is cancelled, so none goes out when the controller joins the bus again. The wait keeps a
		// cancelled frame from staying in the FIFO, where it would be sent after the next Start().
		canHandle->TXBCR = TxBufferAll;
		bool emptied = WaitForTxFifoEmpty();
		status = EnterInitMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Frames that were sent before the stop, whether their event was already in the FIFO or not yet served by the interrupt,
		// are reported as sent. The serial gaps left by the cancelled frames are not reported as failed: the application
		// stopped the controller on purpose.
		uint32 primask = __get_PRIMASK();
		__disable_irq();
		uint32 cancelled = 0;
		uint32 sent = ReadTxEvents(cancelled);
		txResolved = txPlaced;
		__set_PRIMASK(primask);
		for (uint32 i = 0; i < sent; i++) {
			CallTransmitComplete();
		}

		UpdateBusState();
		if (!emptied) {
			return ResultStatus::timeout;
		}

		return ResultStatus::ok;
	}


	// Filters are written into the message RAM and read on every received frame, so they take effect at once, also while the
	// controller runs. A rewritten element is switched off before its new value is written, so no frame matches a mix of
	// the old and the new filter.
	virtual ResultStatus SetFilter(uint8 index, const Filter& filter) override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (index >= GetFilterCount(filter.extended)) {
			return ResultStatus::outOfRange;
		}

		uint32 idMask = filter.extended ? ExtendedIdMask : StandardIdMask;
		if (filter.id > idMask || filter.mask > idMask) {
			return ResultStatus::invalidParameter;
		}

		PrepareFilterRam();
		if (!filter.extended) {
			// A standard element is one word, so the hardware sees either the old or the new filter.
			uint32 element = (FilterClassic << 30) | (FilterStoreRxFifo0 << 27) | ((filter.id & StandardIdMask) << 16) | (filter.mask & StandardIdMask);
			*GetRamWord(StandardFilterOffset + index * StandardFilterSize) = element;
			return ResultStatus::ok;
		}

		volatile uint32 *element = GetRamWord(ExtendedFilterOffset + index * ExtendedFilterSize);
		element[0] = 0;
		element[1] = (FilterClassic << 30) | (filter.mask & ExtendedIdMask);
		element[0] = (FilterStoreRxFifo0 << 29) | (filter.id & ExtendedIdMask);
		return ResultStatus::ok;
	}


	// The call has no identifier type, so the index is cleared in both tables: the standard filter at index, and the
	// extended filter at index when there is one. outOfRange only for an index beyond the standard table.
	virtual ResultStatus ClearFilter(uint8 index) override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (index >= StandardFilterCount) {
			return ResultStatus::outOfRange;
		}

		PrepareFilterRam();
		*GetRamWord(StandardFilterOffset + index * StandardFilterSize) = 0;
		if (index < ExtendedFilterCount) {
			volatile uint32 *element = GetRamWord(ExtendedFilterOffset + index * ExtendedFilterSize);
			element[0] = 0;
			element[1] = 0;
		}

		return ResultStatus::ok;
	}


	virtual uint8 GetFilterCount(bool extended) const override {
		return static_cast<uint8>(extended ? ExtendedFilterCount : StandardFilterCount);
	}


	virtual ResultStatus Send(const Frame& frame) override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		// A frame the bit rate setting cannot carry is refused rather than changed: CAN FD frames need a data bit rate, and
		// their length has to be one of the FD lengths. Remote frames do not exist in CAN FD.
		if (frame.fd) {
			if (parameters.dataBitrate == 0 || frame.remote || !IsFdLength(frame.length)) {
				return ResultStatus::invalidParameter;
			}
		} else {
			if (frame.length > ClassicMaxDataLength || frame.bitRateSwitch) {
				return ResultStatus::invalidParameter;
			}
		}

		if (frame.id > (frame.extended ? ExtendedIdMask : StandardIdMask)) {
			return ResultStatus::invalidParameter;
		}

		if (!IsStarted()) {
			return ResultStatus::notReady;
		}

		// Reading the put index and writing the element must not be interleaved with an interrupt that also sends. PRIMASK is
		// restored rather than set, so Send() also works when it is called from an interrupt.
		uint32 primask = __get_PRIMASK();
		__disable_irq();
		status = TryPlace(frame);
		__set_PRIMASK(primask);
		return status;
	}


	virtual bool CanSend() const override {
		if (!IsStarted()) {
			return false;
		}

		return (canHandle->TXFQS & FDCAN_TXFQS_TFFL_Msk) != 0;
	}


	virtual Result<Frame> Receive() override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (onReceive != nullptr) {
			return ResultStatus::notAvailable;
		}

		if ((canHandle->RXF0S & FDCAN_RXF0S_F0FL_Msk) == 0) {
			return ResultStatus::notAvailable;
		}

		return ReadFifoFrame();
	}


	virtual BusState GetBusState() const override {
		if (!initialized || CheckInstance() != ResultStatus::ok) {
			return BusState::Stopped;
		}

		// Bus-off holds INIT in the hardware, so it is checked before the started state: a controller that waits for its
		// recovery is still bus-off, not stopped.
		uint32 psr = canHandle->PSR;
		if (started && (psr & FDCAN_PSR_BO) != 0) {
			return BusState::BusOff;
		}

		if (!IsStarted()) {
			return BusState::Stopped;
		}

		if ((psr & FDCAN_PSR_EP) != 0) {
			return BusState::ErrorPassive;
		}

		return BusState::ErrorActive;
	}


	virtual ErrorCounters GetErrorCounters() const override {
		ErrorCounters counters;
		if (CheckInstance() != ResultStatus::ok) {
			return counters;
		}

		uint32 ecr = canHandle->ECR;
		counters.transmit = static_cast<uint8>((ecr & FDCAN_ECR_TEC_Msk) >> FDCAN_ECR_TEC_Pos);
		counters.receive = static_cast<uint8>((ecr & FDCAN_ECR_REC_Msk) >> FDCAN_ECR_REC_Pos);
		return counters;
	}


	virtual ResultStatus RecoverFromBusOff() override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (GetBusState() != BusState::BusOff) {
			return ResultStatus::notReady;
		}

		// The hardware holds INIT after bus-off. Clearing it starts the wait for 129 x 11 recessive bits.
		status = LeaveInitMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		UpdateBusState();
		return ResultStatus::ok;
	}


	virtual void IrqHandler() override {
		if (CheckInstance() != ResultStatus::ok) {
			return;
		}

		ServiceTransmit();
		ServiceReceive();
		ServiceStatus();
	}


protected:
	virtual bool IsClockEnabled() const = 0;


	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		// After BeforeInitialization(), because the BSP may enable the clock there.
		status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Stopped before anything can be refused, so a rejected parameter set never leaves the controller on the bus with the
		// old timing.
		status = Stop();
		if (status != ResultStatus::ok) {
			return status;
		}

		initialized = false;

		if (parameters.nominalBitrate == 0 || parameters.nominalBitrate > MaxNominalBitrate) {
			return ResultStatus::invalidParameter;
		}

		if (parameters.dataBitrate > MaxDataBitrate) {
			return ResultStatus::invalidParameter;
		}

		if (parameters.samplePointPermille == 0 || parameters.samplePointPermille >= 1000) {
			return ResultStatus::invalidParameter;
		}

		BitTiming nominal;
		if (!CalculateTiming(inputBusClockHz, parameters.nominalBitrate, parameters.samplePointPermille, NominalLimits, nominal)) {
			return ResultStatus::invalidParameter;
		}

		bool fd = parameters.dataBitrate != 0;
		BitTiming data;
		if (fd && !CalculateTiming(inputBusClockHz, parameters.dataBitrate, parameters.samplePointPermille, DataLimits, data)) {
			return ResultStatus::invalidParameter;
		}

		uint32 tdcOffset = (data.tseg1 + 1) * data.prescaler;
		bool tdc = fd && parameters.dataBitrate >= TdcMinDataBitrate && tdcOffset <= TdcMaxOffset;

		status = EnterInitMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Configuration registers are writable only with CCE set, and TEST only while CCCR.TEST is set. TEST is set for the
		// write in every mode, so that a loopback left over from an earlier configuration is always cleared.
		bool loopback = parameters.mode == Mode::InternalLoopback || parameters.mode == Mode::ExternalLoopback;
		uint32 cccr = (canHandle->CCCR & ~CccrConfigMask) | FDCAN_CCCR_CCE | CastModeFlags() | (parameters.automaticRetransmission ? 0U : FDCAN_CCCR_DAR);
		if (fd) {
			cccr |= FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
		}

		canHandle->CCCR = cccr | FDCAN_CCCR_TEST;
		canHandle->TEST = loopback ? FDCAN_TEST_LBCK : 0U;
		canHandle->CCCR = cccr;

		canHandle->NBTP = ((nominal.sjw - 1U) << FDCAN_NBTP_NSJW_Pos) | ((nominal.prescaler - 1U) << FDCAN_NBTP_NBRP_Pos) | ((nominal.tseg1 - 1U) << FDCAN_NBTP_NTSEG1_Pos) | ((nominal.tseg2 - 1U) << FDCAN_NBTP_NTSEG2_Pos);

		uint32 dbtp = 0;
		uint32 tdcr = 0;
		if (fd) {
			dbtp = ((data.sjw - 1U) << FDCAN_DBTP_DSJW_Pos) | ((data.prescaler - 1U) << FDCAN_DBTP_DBRP_Pos) | ((data.tseg1 - 1U) << FDCAN_DBTP_DTSEG1_Pos) | ((data.tseg2 - 1U) << FDCAN_DBTP_DTSEG2_Pos);
			if (tdc) {
				dbtp |= FDCAN_DBTP_TDC;
				tdcr = tdcOffset << FDCAN_TDCR_TDCO_Pos;
			}
		}

		canHandle->DBTP = dbtp;
		canHandle->TDCR = tdcr;

		// Every filter element is evaluated, so all 28 standard and 8 extended elements are in use. Frames that match no
		// element are rejected (ANFS, ANFE): with no filter set, the port accepts nothing. Rx FIFO 0 is blocking (F0OM = 0):
		// a full FIFO drops the new frame and raises RF0L, instead of overwriting the oldest one.
		// Only the fields the port owns are written, as the HAL does. Remote frames keep RRFS = RRFE = 0, so they are matched
		// against the filters like data frames.
		uint32 rxgfc = (StandardFilterCount << FDCAN_RXGFC_LSS_Pos) | (ExtendedFilterCount << FDCAN_RXGFC_LSE_Pos) | (NonMatchingReject << FDCAN_RXGFC_ANFS_Pos) | (NonMatchingReject << FDCAN_RXGFC_ANFE_Pos);
		MODIFY_REG(canHandle->RXGFC, RxgfcMask, rxgfc);

		// Tx FIFO mode (TFQM = 0): frames leave in the order Send() placed them.
		canHandle->TXBC &= ~FDCAN_TXBC_TFQM;
		MODIFY_REG(canHandle->TSCC, FDCAN_TSCC_TSS, TimestampPerBitTime << FDCAN_TSCC_TSS_Pos);

		// Group RXFIFO0 goes to line 0, all other groups to line 1, so both vectors carry interrupts.
		canHandle->ILS = FDCAN_ILS_RXFIFO1 | FDCAN_ILS_SMSG | FDCAN_ILS_TFERR | FDCAN_ILS_MISC | FDCAN_ILS_BERR | FDCAN_ILS_PERR;
		canHandle->ILE = FDCAN_ILE_EINT0 | FDCAN_ILE_EINT1;
		canHandle->IE = 0;
		canHandle->IR = 0xFFFFFFFFU;

		PrepareFilterRam();
		initialized = true;
		UpdateBusState();
		return AfterInitialization();
	}


private:
	ResultStatus CheckInstance() const {
		if (canHandle == nullptr) {
			return ResultStatus::noInit;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		return ResultStatus::ok;
	}


	bool IsStarted() const {
		return started && initialized && CheckInstance() == ResultStatus::ok && (canHandle->CCCR & FDCAN_CCCR_INIT) == 0;
	}


	bool WaitForCccr(uint32 flag, bool expectSet) const {
		uint64 start = System::GetMs();
		while (((canHandle->CCCR & flag) != 0) != expectSet) {
			if (System::GetMs() - start > TimeoutMs) {
				return false;
			}
		}

		return true;
	}


	bool WaitForTxFifoEmpty() const {
		uint64 start = System::GetMs();
		while ((canHandle->TXFQS & FDCAN_TXFQS_TFFL_Msk) != TxFifoEmptyLevel) {
			if (System::GetMs() - start > TimeoutMs) {
				return false;
			}
		}

		return true;
	}


	ResultStatus EnterInitMode() {
		// The clock stop request has to be cleared first. INIT is only granted once the stop is acknowledged, so both are
		// waited for.
		canHandle->CCCR &= ~FDCAN_CCCR_CSR;
		if (!WaitForCccr(FDCAN_CCCR_CSA, false)) {
			return ResultStatus::timeout;
		}

		canHandle->CCCR |= FDCAN_CCCR_INIT;
		if (!WaitForCccr(FDCAN_CCCR_INIT, true)) {
			return ResultStatus::timeout;
		}

		return ResultStatus::ok;
	}


	ResultStatus LeaveInitMode() {
		canHandle->CCCR &= ~FDCAN_CCCR_INIT;
		if (!WaitForCccr(FDCAN_CCCR_INIT, false)) {
			return ResultStatus::timeout;
		}

		return ResultStatus::ok;
	}


	void UpdateBusState() {
		// Called from thread context and from IrqHandler(). The compare and the store must not be split by an interrupt, or a
		// stale state could overwrite a newer one. The callback runs after PRIMASK is restored.
		uint32 primask = __get_PRIMASK();
		__disable_irq();
		BusState state = GetBusState();
		bool changed = state != lastBusState;
		if (changed) {
			lastBusState = state;
		}

		__set_PRIMASK(primask);
		if (changed) {
			CallBusStateChanged(state);
		}
	}


	uint32 GetRamBase() const {
		uint32 instance = 0;
		#if defined(FDCAN2)
			if (canHandle == FDCAN2) {
				instance = 1;
			}
		#endif

		#if defined(FDCAN3)
			if (canHandle == FDCAN3) {
				instance = 2;
			}
		#endif

		return SRAMCAN_BASE + instance * MessageRamInstanceSize;
	}


	// Message RAM is accessed through volatile 32-bit words only, as the element layout is word based.
	volatile uint32 *GetRamWord(uint32 offset) const {
		return reinterpret_cast<volatile uint32 *>(GetRamBase() + offset);
	}


	// A filter element with the reset value (0) is switched off. The RAM of a fresh instance is not zeroed by the hardware,
	// and LSS and LSE make every element count, so the list is cleared once before the first element is written.
	void PrepareFilterRam() {
		if (filterRamReady) {
			return;
		}

		uint32 words = (ExtendedFilterOffset + ExtendedFilterCount * ExtendedFilterSize) / 4U;
		for (uint32 i = 0; i < words; i++) {
			GetRamWord(i * 4U)[0] = 0;
		}

		filterRamReady = true;
	}


	uint32 CastModeFlags() const {
		switch (parameters.mode) {
			case Mode::Normal:
				return 0;
			case Mode::ListenOnly:
				return FDCAN_CCCR_MON;
			// Loopback runs with TEST and TEST.LBCK. Internal loopback also sets MON, so the frames never reach the TX pin.
			case Mode::InternalLoopback:
				return FDCAN_CCCR_TEST | FDCAN_CCCR_MON;
			case Mode::ExternalLoopback:
				return FDCAN_CCCR_TEST;
		}

		SystemAbort();
		return 0;
	}


	static bool IsFdLength(uint8 length) {
		for (uint8 dlc = 0; dlc < 16; dlc++) {
			if (DlcLengths[dlc] == length) {
				return true;
			}
		}

		return false;
	}


	static uint32 EncodeDlc(uint8 length) {
		for (uint32 dlc = 0; dlc < 16; dlc++) {
			if (DlcLengths[dlc] == length) {
				return dlc;
			}
		}

		return 0;
	}


	static uint8 DecodeLength(uint32 dlc, bool fd) {
		uint8 length = DlcLengths[dlc & 0xF];
		if (!fd && length > ClassicMaxDataLength) {
			length = ClassicMaxDataLength;
		}

		return length;
	}


	static uint32 PackWord(const std::array<uint8, FdMaxDataLength>& data, uint32 start) {
		uint32 word = 0;
		for (uint32 byte = 0; byte < 4; byte++) {
			word |= static_cast<uint32>(data[start + byte]) << (8U * byte);
		}

		return word;
	}


	static Error CastErrorCode(uint32 code) {
		switch (code) {
			case 1:
				return Error::Stuff;
			case 2:
				return Error::Form;
			case 3:
				return Error::Acknowledge;
			case 4:
				return Error::BitRecessive;
			case 5:
				return Error::BitDominant;
			case 6:
				return Error::Crc;
		}

		return Error::Unknown;
	}


	// Finds the timing with the sample point closest to the request among the quanta counts that the clock reaches within
	// the bit rate tolerance. A sample point moved further than MaxSamplePointErrorPermille is refused, not applied silently.
	static bool CalculateTiming(uint32 clockHz, uint32 bitrate, uint32 samplePermille, const TimingLimits& limits, BitTiming& timing) {
		uint64 clock = clockHz;
		uint64 bits = bitrate;
		uint64 bestSampleError = 0;
		uint64 bestErrorPpm = 0;
		bool found = false;

		// More quanta per bit give finer steps for the sample point, so the search starts from the largest count.
		for (uint32 quanta = MaxQuanta; quanta >= limits.minQuanta; quanta--) {
			uint64 divisor = bits * quanta;
			uint64 prescaler = (clock + divisor / 2) / divisor;
			if (prescaler == 0 || prescaler > limits.maxPrescaler) {
				continue;
			}

			uint64 ideal = divisor * prescaler;
			uint64 difference = (clock > ideal) ? (clock - ideal) : (ideal - clock);
			uint64 errorPpm = difference * 1000000 / ideal;
			if (errorPpm > MaxBitrateErrorPpm) {
				continue;
			}

			// The sample point lies after the sync quantum, so the position in quanta is 1 + TSEG1.
			uint64 position = (samplePermille * quanta + 500) / 1000;
			uint64 tseg1 = (position > 0) ? (position - 1) : 0;
			uint64 tseg1Low = (quanta > limits.maxTseg2 + 1) ? (quanta - limits.maxTseg2 - 1) : 1;
			uint64 tseg1High = (quanta - 2 < limits.maxTseg1) ? (quanta - 2) : limits.maxTseg1;
			if (tseg1 < tseg1Low) {
				tseg1 = tseg1Low;
			}

			if (tseg1 > tseg1High) {
				tseg1 = tseg1High;
			}

			uint64 achieved = (tseg1 + 1) * 1000 / quanta;
			uint64 sampleError = (achieved > samplePermille) ? (achieved - samplePermille) : (samplePermille - achieved);
			if (found && (sampleError > bestSampleError || (sampleError == bestSampleError && errorPpm >= bestErrorPpm))) {
				continue;
			}

			uint64 tseg2 = quanta - 1 - tseg1;
			timing.prescaler = static_cast<uint32>(prescaler);
			timing.tseg1 = static_cast<uint32>(tseg1);
			timing.tseg2 = static_cast<uint32>(tseg2);
			timing.sjw = (tseg2 < limits.maxSjw) ? static_cast<uint32>(tseg2) : limits.maxSjw;
			bestSampleError = sampleError;
			bestErrorPpm = errorPpm;
			found = true;
		}

		return found && bestSampleError <= MaxSamplePointErrorPermille;
	}


	uint32 EncodeIdentifier(const Frame& frame) const {
		uint32 identifier = frame.extended ? (ElementExtended | (frame.id & ExtendedIdMask)) : ((frame.id & StandardIdMask) << ElementStandardShift);
		if (frame.remote) {
			identifier |= ElementRemote;
		}

		return identifier;
	}


	ResultStatus TryPlace(const Frame& frame) {
		uint32 txfqs = canHandle->TXFQS;
		if ((txfqs & FDCAN_TXFQS_TFFL_Msk) == 0) {
			return ResultStatus::busy;
		}

		uint32 index = (txfqs & FDCAN_TXFQS_TFQPI_Msk) >> FDCAN_TXFQS_TFQPI_Pos;
		uint8 serial = static_cast<uint8>(txPlaced + 1U);
		volatile uint32 *element = GetRamWord(TxFifoOffset + index * TxElementSize);

		// Data first, header last: the header holds the marker, and the request (TXBAR) comes after both.
		uint32 wordCount = (frame.length + 3U) / 4U;
		for (uint32 i = 0; i < wordCount; i++) {
			element[ElementHeaderSize / 4U + i] = PackWord(frame.data, i * 4U);
		}

		// EFC is set on every element. Without it the hardware writes no Tx event, and every frame would look failed.
		uint32 header1 = (static_cast<uint32>(serial) << ElementMarkerShift) | (EncodeDlc(frame.length) << ElementDlcShift) | ElementStoreEvent;
		if (frame.fd) {
			header1 |= ElementFdFormat;
		}

		if (frame.bitRateSwitch) {
			header1 |= ElementBitRateSwitch;
		}

		element[0] = EncodeIdentifier(frame);
		element[1] = header1;
		__DSB();
		canHandle->TXBAR = 1U << index;
		txPlaced = serial;
		return ResultStatus::ok;
	}


	Frame ReadFifoFrame() {
		uint32 getIndex = (canHandle->RXF0S & FDCAN_RXF0S_F0GI_Msk) >> FDCAN_RXF0S_F0GI_Pos;
		volatile uint32 *element = GetRamWord(RxFifo0Offset + getIndex * RxElementSize);
		uint32 header0 = element[0];
		uint32 header1 = element[1];

		Frame frame;
		frame.extended = (header0 & ElementExtended) != 0;
		frame.id = frame.extended ? (header0 & ExtendedIdMask) : ((header0 >> ElementStandardShift) & StandardIdMask);
		frame.fd = (header1 & ElementFdFormat) != 0;
		// RTR does not exist in CAN FD, so a set bit in an FD element is ignored.
		frame.remote = !frame.fd && (header0 & ElementRemote) != 0;
		frame.bitRateSwitch = (header1 & ElementBitRateSwitch) != 0;
		frame.length = DecodeLength((header1 >> ElementDlcShift) & 0xF, frame.fd);
		frame.timestamp = header1 & 0xFFFF;

		uint32 wordCount = (frame.length + 3U) / 4U;
		for (uint32 i = 0; i < wordCount; i++) {
			uint32 word = element[ElementHeaderSize / 4U + i];
			for (uint32 byte = 0; byte < 4; byte++) {
				frame.data[i * 4U + byte] = static_cast<uint8>(word >> (8U * byte));
			}
		}

		// Acknowledging the index frees the element for the hardware.
		canHandle->RXF0A = getIndex;
		return frame;
	}


	// Pops the Tx event FIFO. Returns the number of transmitted frames it reported; a frame that was skipped in the serial
	// numbers counts as failed.
	uint32 ReadTxEvents(uint32& failures) {
		uint32 successes = 0;
		while ((canHandle->TXEFS & FDCAN_TXEFS_EFFL_Msk) != 0) {
			uint32 getIndex = (canHandle->TXEFS & FDCAN_TXEFS_EFGI_Msk) >> FDCAN_TXEFS_EFGI_Pos;
			uint32 serial = (GetRamWord(TxEventOffset + getIndex * TxEventSize)[1] >> ElementMarkerShift) & 0xFF;
			canHandle->TXEFA = getIndex;

			// Serial numbers of the frames still in flight run from txResolved + 1 up to txPlaced. An event outside that
			// range belongs to a frame that is already resolved and is ignored.
			uint8 inFlight = static_cast<uint8>(txPlaced - txResolved);
			uint8 skipped = static_cast<uint8>(serial - txResolved - 1U);
			if (skipped < inFlight) {
				failures += skipped;
				txResolved = static_cast<uint8>(serial);
				successes++;
			}
		}

		return successes;
	}


	void ServiceTransmit() {
		uint32 flags = canHandle->IR & (FDCAN_IR_TEFN | FDCAN_IR_TFE);
		if (flags == 0) {
			return;
		}

		// Cleared before the FIFO is read: an event that arrives meanwhile stays pending and the next call serves it.
		canHandle->IR = flags;

		uint32 successes = 0;
		uint32 failures = 0;
		uint32 primask = __get_PRIMASK();
		__disable_irq();
		successes += ReadTxEvents(failures);
		if ((flags & FDCAN_IR_TFE) != 0 && (canHandle->TXFQS & FDCAN_TXFQS_TFFL_Msk) == TxFifoEmptyLevel) {
			// The FIFO is empty, so every frame still in flight is taken as ended without an event. This relies on the element
			// release assumption stated at the top of this file; if a frame that is still on the bus is reported as failed on
			// hardware, the assumption is wrong. The events are read again: one may have been written between the first read
			// and the empty check.
			successes += ReadTxEvents(failures);
			failures += static_cast<uint8>(txPlaced - txResolved);
			txResolved = txPlaced;
		}

		__set_PRIMASK(primask);

		for (uint32 i = 0; i < successes; i++) {
			CallTransmitComplete();
		}

		for (uint32 i = 0; i < failures; i++) {
			CallError(Error::TransmitFailed);
		}
	}


	void ServiceReceive() {
		uint32 flags = canHandle->IR & (FDCAN_IR_RF0N | FDCAN_IR_RF0L);
		if (flags == 0) {
			return;
		}

		canHandle->IR = flags;
		if ((flags & FDCAN_IR_RF0L) != 0) {
			CallError(Error::ReceiveOverrun);
		}

		// Without onReceive the frames stay in the FIFO for Receive(). Reading them here would take them away from it.
		if (onReceive == nullptr) {
			return;
		}

		while ((canHandle->RXF0S & FDCAN_RXF0S_F0FL_Msk) != 0) {
			Frame frame = ReadFifoFrame();
			CallReceive(frame);
		}
	}


	void ServiceStatus() {
		uint32 flags = canHandle->IR & (FDCAN_IR_BO | FDCAN_IR_EW | FDCAN_IR_EP | FDCAN_IR_PEA | FDCAN_IR_PED);
		if (flags == 0) {
			return;
		}

		canHandle->IR = flags;

		uint32 psr = canHandle->PSR;
		uint32 lec = psr & FDCAN_PSR_LEC_Msk;
		uint32 dlec = (psr & FDCAN_PSR_DLEC_Msk) >> FDCAN_PSR_DLEC_Pos;
		if ((flags & FDCAN_IR_PEA) != 0 && lec != ErrorCodeNone && lec != ErrorCodeNoChange) {
			CallError(CastErrorCode(lec));
		}

		if ((flags & FDCAN_IR_PED) != 0 && dlec != ErrorCodeNone && dlec != ErrorCodeNoChange) {
			CallError(CastErrorCode(dlec));
		}

		// Clearing INIT starts the hardware's recovery from bus-off. The recovery itself takes 129 x 11 recessive bits, so
		// the handler does not wait for it. Without automatic recovery the application calls RecoverFromBusOff(). The hardware
		// raises no interrupt when the recovery ends, so onBusStateChanged(ErrorActive) comes with the next status interrupt.
		// GetBusState() reads the hardware and is current at any time.
		if ((flags & FDCAN_IR_BO) != 0 && started && parameters.automaticBusOffRecovery) {
			canHandle->CCCR &= ~FDCAN_CCCR_INIT;
		}

		UpdateBusState();
	}
};


#endif
