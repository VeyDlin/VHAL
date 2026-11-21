#pragma once
#include <Adapter/CANAdapter.h>


// bxCAN on STM32F4 (classic CAN 2.0A/B only). SetParameters() refuses a non-zero dataBitrate with notSupported, and Send()
// refuses CAN FD frames with invalidParameter.
// CAN1 and CAN2 share one set of 28 filter banks. CAN2 owns the banks from Can2StartBank up, CAN1 the banks below it.
// The filter registers sit in the CAN1 block, so CAN2 also needs the CAN1 clock.
//
// The BSP must, before the adapter is used:
//   - Clock: enable the APB1 clock of the controller in RCC->APB1ENR: CAN1EN for CAN1, CAN2EN for CAN2 (CAN1EN as well,
//     see above). The adapter only checks these bits and returns notReady when one is off; it never configures the
//     clock tree. Pass the APB1 clock in Hz to the constructor, the bit timing is computed from it.
//   - Pins: put the TX and RX pins in the alternate function AF9 (see the alternate function table of the datasheet).
//   - NVIC: enable the four vectors of the controller and call CANAdapterF4::IrqHandler() from each of them:
//       CAN1: CAN1_TX_IRQn, CAN1_RX0_IRQn, CAN1_RX1_IRQn, CAN1_SCE_IRQn
//       CAN2: CAN2_TX_IRQn, CAN2_RX0_IRQn, CAN2_RX1_IRQn, CAN2_SCE_IRQn
//   - Assign onReceive before Start(). A handler assigned while started is not served at once: the FIFO 0 interrupt is
//     switched on by the next IrqHandler() call, so frames already in FIFO 0 wait for an interrupt of this controller
//     (a transmit, error or overrun event). Receive() takes them while no handler is set.
//   - Frames are accepted into FIFO 0 only. The RX1 vector serves the FIFO 1 overrun flags, and nothing is routed to FIFO 1.
//
// Timestamps: Frame::timestamp is the 16-bit RDTR.TIME value captured at the start of frame, in bit times. TTCM is off.
// It wraps every 65536 bit times.
#if defined(CAN1)

using ACAN = class CANAdapterF4;


class CANAdapterF4: public CANAdapter<CAN_TypeDef> {
	static_assert(CAN_TSR_RQCP1 == (CAN_TSR_RQCP0 << 8), "bxCAN: the mailbox status flags are 8 bits apart");
	static_assert(CAN_TSR_TXOK1 == (CAN_TSR_TXOK0 << 8), "bxCAN: the mailbox status flags are 8 bits apart");
	static_assert(CAN_TSR_ALST1 == (CAN_TSR_ALST0 << 8), "bxCAN: the mailbox status flags are 8 bits apart");
	static_assert(CAN_TSR_TERR1 == (CAN_TSR_TERR0 << 8), "bxCAN: the mailbox status flags are 8 bits apart");

private:
	struct BitTiming {
		uint32 prescaler;
		uint32 ts1;
		uint32 ts2;
		uint32 sjw;
	};


	static inline constexpr uint8 MailboxCount = 3;
	static inline constexpr uint8 FilterBankTotal = 28;
	static inline constexpr uint8 Can2StartBank = 14;
	static inline constexpr uint32 StandardIdMask = 0x7FF;
	static inline constexpr uint32 ExtendedIdMask = 0x1FFFFFFF;
	static inline constexpr uint32 MaxBitrate = 1000000;
	// CAN bit rate tolerance of 0.5 %, in millionths of the bit rate.
	static inline constexpr uint64 MaxBitrateErrorPpm = 5000;
	// Largest distance between the requested and the achievable sample point, in permille. TS1 is limited by the register
	// range, so with few quanta per bit the sample point cannot always be reached. 20 permille keeps 85.7 % for a request
	// of 87.5 %, and refuses a timing that would be moved further than that.
	static inline constexpr uint64 MaxSamplePointErrorPermille = 20;
	static inline constexpr uint32 MinQuanta = 8;
	static inline constexpr uint32 MaxQuanta = 25;
	static inline constexpr uint32 MaxPrescaler = 1024;
	static inline constexpr uint32 MaxTs1 = 16;
	static inline constexpr uint32 MaxTs2 = 8;
	static inline constexpr uint32 MaxSjw = 4;
	static inline constexpr uint64 TimeoutMs = 10;
	bool initialized = false;
	BusState lastBusState = BusState::Stopped;


public:
	CANAdapterF4() = default;


	CANAdapterF4(CAN_TypeDef *can, uint32 kernelClockHz):CANAdapter(can, kernelClockHz) {
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

		// Interrupts are enabled here rather than in Initialization(). The FIFO 0 interrupt stays off without onReceive,
		// otherwise it would fire on frames that Receive() is meant to take. IrqHandler() turns it on once onReceive is set.
		uint32 interrupts = CAN_IER_TMEIE | CAN_IER_FOVIE0 | CAN_IER_FOVIE1 | CAN_IER_ERRIE | CAN_IER_EWGIE | CAN_IER_EPVIE | CAN_IER_BOFIE | CAN_IER_LECIE;
		if (onReceive != nullptr) {
			interrupts |= CAN_IER_FMPIE0;
		}

		canHandle->IER = interrupts;

		canHandle->MCR &= ~CAN_MCR_INRQ;
		if (!WaitForInitAcknowledge(false)) {
			return ResultStatus::timeout;
		}

		UpdateBusState();
		return ResultStatus::ok;
	}


	virtual ResultStatus Stop() override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!IsStarted()) {
			return ResultStatus::ok;
		}

		// A frame still waiting in a mailbox would go out as soon as the controller joins the bus again. Abort it now.
		canHandle->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;

		status = EnterInitMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		UpdateBusState();
		return ResultStatus::ok;
	}


	virtual ResultStatus SetFilter(uint8 index, const Filter& filter) override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (index >= GetBankCount()) {
			return ResultStatus::outOfRange;
		}

		if (filter.id > (filter.extended ? ExtendedIdMask : StandardIdMask)) {
			return ResultStatus::invalidParameter;
		}

		// The filter registers are only writable in filter initialization mode, and the hardware does not take frames
		// through a bank that is being rewritten. Refuse the change while the controller is on the bus.
		if (IsStarted()) {
			return ResultStatus::busy;
		}

		uint32 bank = GetBankBase() + index;
		uint32 bankBit = 1U << bank;

		EnterFilterInit();
		CAN1->FA1R &= ~bankBit;
		// 32-bit scale: one identifier and one mask register per bank.
		CAN1->FS1R |= bankBit;
		// Mask mode: a frame passes when its bits match id wherever mask has a 1.
		CAN1->FM1R &= ~bankBit;
		// Accepted frames go to FIFO 0.
		CAN1->FFA1R &= ~bankBit;

		uint32 idRegister = EncodeIdentifier(filter.id, filter.extended);
		if (filter.extended) {
			idRegister |= CAN_TI0R_IDE;
		}

		CAN1->sFilterRegister[bank].FR1 = idRegister;

		// The IDE bit of the mask is always compared, so a standard filter never passes an extended frame and the other way round.
		CAN1->sFilterRegister[bank].FR2 = EncodeIdentifier(filter.mask, filter.extended) | CAN_TI0R_IDE;
		CAN1->FA1R |= bankBit;
		LeaveFilterInit();
		return ResultStatus::ok;
	}


	virtual ResultStatus ClearFilter(uint8 index) override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (index >= GetBankCount()) {
			return ResultStatus::outOfRange;
		}

		if (IsStarted()) {
			return ResultStatus::busy;
		}

		uint32 bankBit = 1U << (GetBankBase() + index);
		EnterFilterInit();
		CAN1->FA1R &= ~bankBit;
		LeaveFilterInit();
		return ResultStatus::ok;
	}


	virtual uint8 GetFilterCount(bool) const override {
		// The banks are shared by both identifier types, so the count does not depend on the type.
		return GetBankCount();
	}


	virtual ResultStatus Send(const Frame& frame) override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		// The port has no CAN FD, and every fd frame reaching here has no data bitrate (Initialization() refuses one).
		if (frame.fd) {
			return ResultStatus::invalidParameter;
		}

		if (frame.length > ClassicMaxDataLength) {
			return ResultStatus::invalidParameter;
		}

		if (frame.id > (frame.extended ? ExtendedIdMask : StandardIdMask)) {
			return ResultStatus::invalidParameter;
		}

		if (!IsStarted()) {
			return ResultStatus::notReady;
		}

		// Choosing a free mailbox and writing it must not be interleaved with an interrupt that also sends: the interrupt
		// would pick the same mailbox and overwrite the frame in flight. The PRIMASK is restored, not set, so that Send()
		// also works when it is called from an interrupt.
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

		return (canHandle->TSR & CAN_TSR_TME) != 0;
	}


	virtual Result<Frame> Receive() override {
		ResultStatus status = CheckInstance();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (onReceive != nullptr) {
			return ResultStatus::notAvailable;
		}

		if ((canHandle->RF0R & CAN_RF0R_FMP0) == 0) {
			return ResultStatus::notAvailable;
		}

		return ReadFifoFrame();
	}


	virtual BusState GetBusState() const override {
		if (!IsStarted()) {
			return BusState::Stopped;
		}

		uint32 esr = canHandle->ESR;
		if ((esr & CAN_ESR_BOFF) != 0) {
			return BusState::BusOff;
		}

		if ((esr & CAN_ESR_EPVF) != 0) {
			return BusState::ErrorPassive;
		}

		return BusState::ErrorActive;
	}


	virtual ErrorCounters GetErrorCounters() const override {
		ErrorCounters counters;
		if (CheckInstance() != ResultStatus::ok) {
			return counters;
		}

		uint32 esr = canHandle->ESR;
		counters.transmit = static_cast<uint8>((esr & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos);
		counters.receive = static_cast<uint8>((esr & CAN_ESR_REC) >> CAN_ESR_REC_Pos);
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

		// With automatic bus-off recovery off, bxCAN leaves bus-off when the application toggles initialization mode.
		// The wait for 128 x 11 recessive bits starts when initialization mode is left.
		status = EnterInitMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		canHandle->MCR &= ~CAN_MCR_INRQ;
		if (!WaitForInitAcknowledge(false)) {
			return ResultStatus::timeout;
		}

		UpdateBusState();
		return ResultStatus::ok;
	}


	virtual void IrqHandler() override {
		if (CheckInstance() != ResultStatus::ok) {
			return;
		}

		// The four vectors share this handler. Each service looks only at its own flags, so calling it from any vector
		// is safe. Frames are accepted into FIFO 0 only, so FIFO 1 is served for its overrun flags alone.
		ServiceTransmit();
		ServiceReceive();
		ServiceReceiveFifo1();
		ServiceStatus();
	}


protected:
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
		// old timing. Stop() only acts while initialized is set, so the flag is cleared after it.
		status = Stop();
		if (status != ResultStatus::ok) {
			return status;
		}

		initialized = false;

		// Classic CAN only. A data bitrate means the caller expects CAN FD, which this controller does not have.
		if (parameters.dataBitrate != 0) {
			return ResultStatus::notSupported;
		}

		BitTiming timing = {};
		if (!CalculateBitTiming(timing)) {
			return ResultStatus::invalidParameter;
		}

		status = EnterInitMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		// MCR and BTR are writable only in initialization mode, so the controller stays stopped here and Start() leaves it.
		canHandle->MCR = CAN_MCR_INRQ | CastMcrFlags();

		uint32 btr = CastModeFlags();
		btr |= (timing.sjw - 1) << CAN_BTR_SJW_Pos;
		btr |= (timing.ts1 - 1) << CAN_BTR_TS1_Pos;
		btr |= (timing.ts2 - 1) << CAN_BTR_TS2_Pos;
		btr |= timing.prescaler - 1;
		canHandle->BTR = btr;

		initialized = true;
		UpdateBusState();
		return AfterInitialization();
	}


private:
	ResultStatus CheckInstance() const {
		if (canHandle == nullptr) {
			return ResultStatus::noInit;
		}

		if (canHandle != CAN1 && !IsCan2()) {
			return ResultStatus::notSupported;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		return ResultStatus::ok;
	}


	bool IsCan2() const {
		#if defined(CAN2)
			return canHandle == CAN2;
		#else
			return false;
		#endif
	}


	bool IsClockEnabled() const {
		bool can1Enabled = (RCC->APB1ENR & RCC_APB1ENR_CAN1EN) != 0;

		#if defined(CAN2)
			if (IsCan2()) {
				return can1Enabled && (RCC->APB1ENR & RCC_APB1ENR_CAN2EN) != 0;
			}
		#endif

		return can1Enabled;
	}


	bool IsInitAcknowledged() const {
		return (canHandle->MSR & CAN_MSR_INAK) != 0;
	}


	bool IsStarted() const {
		// After reset bxCAN is in sleep mode with INAK clear, so the hardware alone cannot tell that no SetParameters() ran.
		return initialized && CheckInstance() == ResultStatus::ok && !IsInitAcknowledged();
	}


	bool WaitForInitAcknowledge(bool expected) const {
		uint64 start = System::GetMs();
		while (IsInitAcknowledged() != expected) {
			if (System::GetMs() - start > TimeoutMs) {
				return false;
			}
		}

		return true;
	}


	ResultStatus EnterInitMode() {
		// SLEEP must be cleared for INRQ to take effect.
		canHandle->MCR = (canHandle->MCR & ~CAN_MCR_SLEEP) | CAN_MCR_INRQ;
		if (!WaitForInitAcknowledge(true)) {
			return ResultStatus::timeout;
		}

		return ResultStatus::ok;
	}


	void UpdateBusState() {
		// Called from thread context and from the ServiceStatus() interrupt. The compare and the store must not be split by
		// an interrupt, or a stale state could overwrite a newer one. The callback runs after PRIMASK is restored.
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


	uint8 GetBankBase() const {
		if (IsCan2()) {
			return Can2StartBank;
		}

		return 0;
	}


	uint8 GetBankCount() const {
		if (IsCan2()) {
			return static_cast<uint8>(FilterBankTotal - Can2StartBank);
		}

		return Can2StartBank;
	}


	void EnterFilterInit() {
		// CAN2SB decides which banks CAN2 owns. It can be written only in filter initialization mode.
		CAN1->FMR = (CAN1->FMR & ~CAN_FMR_CAN2SB) | (static_cast<uint32>(Can2StartBank) << CAN_FMR_CAN2SB_Pos) | CAN_FMR_FINIT;
	}


	void LeaveFilterInit() {
		CAN1->FMR &= ~CAN_FMR_FINIT;
	}


	static uint32 EncodeIdentifier(uint32 id, bool extended) {
		// The identifier sits in the same place in a mailbox, a FIFO entry and a 32-bit filter register.
		if (extended) {
			return (id & ExtendedIdMask) << CAN_TI0R_EXID_Pos;
		}

		return (id & StandardIdMask) << CAN_TI0R_STID_Pos;
	}


	uint32 CastModeFlags() const {
		switch (parameters.mode) {
			case Mode::Normal:
				return 0;
			case Mode::ListenOnly:
				return CAN_BTR_SILM;
			// Silent and loopback together keep the TX pin recessive and do not touch the bus.
			case Mode::InternalLoopback:
				return CAN_BTR_LBKM | CAN_BTR_SILM;
			case Mode::ExternalLoopback:
				return CAN_BTR_LBKM;
		}

		SystemAbort();
		return 0;
	}


	uint32 CastMcrFlags() const {
		// RFLM: when the FIFO is full the new frame is dropped and FOVR0 is raised, instead of overwriting the oldest one.
		uint32 flags = CAN_MCR_RFLM;
		if (!parameters.automaticRetransmission) {
			flags |= CAN_MCR_NART;
		}

		if (parameters.automaticBusOffRecovery) {
			flags |= CAN_MCR_ABOM;
		}

		return flags;
	}


	bool CalculateBitTiming(BitTiming& timing) const {
		if (parameters.nominalBitrate == 0 || parameters.nominalBitrate > MaxBitrate) {
			return false;
		}

		if (parameters.samplePointPermille == 0 || parameters.samplePointPermille >= 1000) {
			return false;
		}

		uint64 clock = inputBusClockHz;
		uint64 bitrate = parameters.nominalBitrate;
		uint64 samplePoint = parameters.samplePointPermille;
		uint64 bestSampleError = 0;
		uint64 bestErrorPpm = 0;
		bool found = false;

		// More quanta per bit give finer steps for the sample point, so the search starts from the largest count.
		for (uint32 quanta = MaxQuanta; quanta >= MinQuanta; quanta--) {
			uint64 divisor = bitrate * quanta;
			uint64 prescaler = (clock + divisor / 2) / divisor;
			if (prescaler == 0 || prescaler > MaxPrescaler) {
				continue;
			}

			// Relative bit rate error in millionths: the clock that gives exactly the bitrate is ideal.
			uint64 ideal = divisor * prescaler;
			uint64 difference = (clock > ideal) ? (clock - ideal) : (ideal - clock);
			uint64 errorPpm = difference * 1000000 / ideal;
			if (errorPpm > MaxBitrateErrorPpm) {
				continue;
			}

			// The sample point is after the sync segment (1 quantum), so TS1 + 1 quanta is the sample point position.
			uint64 ts1Plus1 = (samplePoint * quanta + 500) / 1000;
			uint64 ts1Low = (quanta > MaxTs2 + 1) ? (quanta - MaxTs2 - 1) : 1;
			uint64 ts1High = (quanta - 2 < MaxTs1) ? (quanta - 2) : MaxTs1;
			uint64 ts1 = (ts1Plus1 > 0) ? (ts1Plus1 - 1) : 0;
			if (ts1 < ts1Low) {
				ts1 = ts1Low;
			}

			if (ts1 > ts1High) {
				ts1 = ts1High;
			}

			// The sample point this TS1 really gives, in permille. The candidate closest to the request wins; the bit rate
			// error only breaks a tie, because the bit rate tolerance has already been applied above.
			uint64 achieved = (ts1 + 1) * 1000 / quanta;
			uint64 sampleError = (achieved > samplePoint) ? (achieved - samplePoint) : (samplePoint - achieved);
			if (found && (sampleError > bestSampleError || (sampleError == bestSampleError && errorPpm >= bestErrorPpm))) {
				continue;
			}

			timing.prescaler = static_cast<uint32>(prescaler);
			timing.ts1 = static_cast<uint32>(ts1);
			timing.ts2 = static_cast<uint32>(quanta - 1 - ts1);
			timing.sjw = (timing.ts2 < MaxSjw) ? timing.ts2 : MaxSjw;
			bestSampleError = sampleError;
			bestErrorPpm = errorPpm;
			found = true;
		}

		// A timing whose sample point is moved further than the limit is refused, not applied silently.
		return found && bestSampleError <= MaxSamplePointErrorPermille;
	}


	static Error CastLastErrorCode(uint32 code) {
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


	ResultStatus TryPlace(const Frame& frame) {
		uint8 index = 0;
		uint32 tsr = canHandle->TSR;
		if ((tsr & CAN_TSR_TME0) != 0) {
			index = 0;
		} else if ((tsr & CAN_TSR_TME1) != 0) {
			index = 1;
		} else if ((tsr & CAN_TSR_TME2) != 0) {
			index = 2;
		} else {
			return ResultStatus::busy;
		}

		uint32 low = 0;
		uint32 high = 0;
		for (uint8 i = 0; i < 4; i++) {
			low |= static_cast<uint32>(frame.data[i]) << (8U * i);
			high |= static_cast<uint32>(frame.data[i + 4U]) << (8U * i);
		}

		// Data and length go first: setting TXRQ in TIR starts the transmission.
		CAN_TxMailBox_TypeDef& mailbox = canHandle->sTxMailBox[index];
		mailbox.TDTR = frame.length & CAN_TDT0R_DLC;
		mailbox.TDLR = low;
		mailbox.TDHR = high;

		uint32 identifier = EncodeIdentifier(frame.id, frame.extended);
		if (frame.extended) {
			identifier |= CAN_TI0R_IDE;
		}

		if (frame.remote) {
			identifier |= CAN_TI0R_RTR;
		}

		mailbox.TIR = identifier | CAN_TI0R_TXRQ;
		return ResultStatus::ok;
	}


	Frame ReadFifoFrame() {
		const CAN_FIFOMailBox_TypeDef& mailbox = canHandle->sFIFOMailBox[0];

		Frame frame;
		frame.extended = (mailbox.RIR & CAN_RI0R_IDE) != 0;
		frame.remote = (mailbox.RIR & CAN_RI0R_RTR) != 0;
		if (frame.extended) {
			frame.id = (mailbox.RIR >> CAN_RI0R_EXID_Pos) & ExtendedIdMask;
		} else {
			frame.id = (mailbox.RIR >> CAN_RI0R_STID_Pos) & StandardIdMask;
		}

		// DLC 9 to 15 mean 8 bytes on classic CAN.
		uint32 length = mailbox.RDTR & CAN_RDT0R_DLC;
		frame.length = static_cast<uint8>((length > ClassicMaxDataLength) ? ClassicMaxDataLength : length);
		frame.timestamp = (mailbox.RDTR & CAN_RDT0R_TIME) >> CAN_RDT0R_TIME_Pos;

		uint32 low = mailbox.RDLR;
		uint32 high = mailbox.RDHR;
		for (uint8 i = 0; i < 4; i++) {
			frame.data[i] = static_cast<uint8>(low >> (8U * i));
			frame.data[i + 4U] = static_cast<uint8>(high >> (8U * i));
		}

		// Releasing the output slot lets the hardware put the next frame in it.
		canHandle->RF0R = CAN_RF0R_RFOM0;
		return frame;
	}


	void ServiceTransmit() {
		uint32 tsr = canHandle->TSR;
		for (uint8 index = 0; index < MailboxCount; index++) {
			uint32 shift = 8U * index;
			uint32 requestComplete = CAN_TSR_RQCP0 << shift;
			if ((tsr & requestComplete) == 0) {
				continue;
			}

			uint32 transmitOk = CAN_TSR_TXOK0 << shift;
			uint32 failure = (CAN_TSR_ALST0 | CAN_TSR_TERR0) << shift;
			bool transmitted = (tsr & transmitOk) != 0;
			bool failed = (tsr & failure) != 0;

			// Clear before the callbacks: a callback may send into this mailbox again.
			canHandle->TSR = requestComplete | transmitOk | failure;

			if (failed) {
				CallError(Error::TransmitFailed);
			} else if (transmitted) {
				CallTransmitComplete();
			}
		}
	}


	void ServiceReceive() {
		uint32 rf0r = canHandle->RF0R;
		uint32 flags = rf0r & (CAN_RF0R_FOVR0 | CAN_RF0R_FULL0);
		if (flags != 0) {
			canHandle->RF0R = flags;
		}

		if ((rf0r & CAN_RF0R_FOVR0) != 0) {
			CallError(Error::ReceiveOverrun);
		}

		// Without onReceive the frames stay in the FIFO for Receive(). Reading them here would take them away from it.
		// The FIFO interrupt is switched off too: while FMP0 stays set and the interrupt stays enabled, the vector would
		// re-enter forever. With onReceive set, the interrupt is switched on again here, so a handler assigned after
		// Start() is served from the next call of IrqHandler() on any of the vectors.
		if (onReceive == nullptr) {
			canHandle->IER &= ~CAN_IER_FMPIE0;
			return;
		}

		canHandle->IER |= CAN_IER_FMPIE0;

		while ((canHandle->RF0R & CAN_RF0R_FMP0) != 0) {
			Frame frame = ReadFifoFrame();
			CallReceive(frame);
		}
	}


	// FIFO 1 gets no frames from the filters of this port. Its overrun flag is still served, so the RX1 vector does work.
	void ServiceReceiveFifo1() {
		uint32 rf1r = canHandle->RF1R;
		uint32 flags = rf1r & (CAN_RF1R_FOVR1 | CAN_RF1R_FULL1);
		if (flags == 0) {
			return;
		}

		canHandle->RF1R = flags;

		if ((rf1r & CAN_RF1R_FOVR1) != 0) {
			CallError(Error::ReceiveOverrun);
		}
	}


	void ServiceStatus() {
		if ((canHandle->MSR & CAN_MSR_ERRI) == 0) {
			return;
		}

		// ERRI is cleared by writing 1 to it.
		canHandle->MSR = CAN_MSR_ERRI;

		uint32 lastErrorCode = (canHandle->ESR & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos;
		if (lastErrorCode != 0) {
			// LEC is set by hardware and cleared by software writing 000. The hardware has no atomic read-and-clear, so a code
			// that is set between the read above and the write below is lost. A new error raises ERRI again, so the interrupt
			// re-enters and the bus state is still updated. Only the error code of that one error is lost.
			canHandle->ESR &= ~CAN_ESR_LEC;
			CallError(CastLastErrorCode(lastErrorCode));
		}

		UpdateBusState();
	}
};


#endif
