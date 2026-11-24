#include <DemoVub/DemoVub.h>
#include <DemoModbus/WrappingClock.h>
#include <DemoVub/Descriptors/VubDescriptors.h>
#include <DemoVub/Echo/EchoQueue.h>
#include <DemoVub/Function/VubFunctions.h>
#include <DemoVub/Status/VubStatus.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Framing/LengthPrefix.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Link/BulkLink.h>
#include <Drivers/Interface/VUB/Link/CdcLink.h>
#include <Drivers/Interface/VUB/Link/HidLink.h>
#include <Drivers/Interface/VUB/Link/UartLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>


namespace DemoVub {
	constexpr uint32 StatusMagic = 0x56554253U;
	constexpr uint32 StageStarted = 1;
	constexpr uint32 StageSerial = 2;
	constexpr uint32 StageSession = 3;
	constexpr uint32 StageUsb = 4;
	constexpr uint32 StageServing = 5;
	constexpr uint32 SessionFromRandom = 1;
	constexpr uint32 SessionFromFallback = 2;
	constexpr uint32 LineBaud = 115200;
	// Endpoint 0 and the seven endpoint numbers the builder assigns. The G4 port opens numbers below EndpointCount only.
	constexpr uint8 EndpointCount = 8;
	constexpr uint64 StatusIntervalMs = 100;
	// Every chain carries payloads of up to PayloadBytes, and each stack's echo queue holds EchoSlots of them.
	constexpr std::size_t PayloadBytes = 32;
	constexpr std::size_t EchoSlots = 4;
	constexpr std::size_t UartIndex = 0;
	constexpr std::size_t CdcIndex = 1;
	constexpr std::size_t BulkIndex = 2;
	constexpr std::size_t HidIndex = 3;
	constexpr std::size_t StackCount = 4;


	// What the main loop keeps for one stack. The handlers and the echo run in the main loop (the stack's Process runs them), so the record
	// is plain data; the status block copies it for SWD.
	struct StackRecord {
		EchoQueue<PayloadBytes, EchoSlots> echo;
		uint32 session = 0;
		uint32 received = 0;
		uint32 echoed = 0;
		uint32 echoRefused = 0;
		uint32 echoDropped = 0;
		uint32 outcomesOk = 0;
		uint32 outcomesFailed = 0;
		uint32 restarts = 0;
		uint32 lastSendStatus = 0;
		uint32 lastOutcomeStatus = 0;
	};


	static std::array<StackRecord, StackCount> records{};
	static SerialPort serialFunction(VubUsb::Function(0));
	static VendorChannel vendorFunction(VubUsb::Function(1));
	static VendorHid hidFunction(VubUsb::Function(2), VubHidReport::Info);
	static std::array<USBFunction*, 3> functions = { &serialFunction, &vendorFunction, &hidFunction };
	static USBDevice usbDevice(BSP::usbAdapter, VubUsb::Set, functions);
	// The MCU's unique ID as the serial number, kept in static storage as the device requires.
	static std::array<uint8, 2 + 4 * BSP::UniqueIdSize> serialDescriptor{};
	// The microsecond clock of the stacks: the cycle counter with its wraps counted (WrappingClock), as the Modbus board check does.
	static WrappingClock cycleClock;


	// The receive handler of a stack: the payload is copied to the stack's echo queue, and the main loop sends it back. A full queue
	// counts the payload as dropped; the delivery layer has acknowledged it already, so the drop is visible in the status block.
	template<std::size_t Index>
	void ReceiveFrom(const VUB::Meta&, std::span<const uint8> payload) {
		StackRecord& record = records[Index];
		record.received++;
		if (!record.echo.Push(payload)) {
			record.echoDropped++;
		}
	}


	// The delivery outcome of an echo this device sent, once per packet.
	template<std::size_t Index>
	void OutcomeOf(const VUB::Outcome& outcome) {
		StackRecord& record = records[Index];
		if (outcome.status == ResultStatus::ok) {
			record.outcomesOk++;
		} else {
			record.outcomesFailed++;
		}

		record.lastOutcomeStatus = static_cast<uint32>(outcome.status);
	}


	// The peer lost the session of this stack, and the layer starts a session again.
	template<std::size_t Index>
	void RestartOf() {
		records[Index].restarts++;
	}


	// Microseconds from the cycle counter. The receive interrupt of the UART does not read the clock, but the critical section is kept
	// as the Modbus check has it, so the read is safe whichever context calls it.
	static uint32 MicrosecondClock() {
		System::CriticalSection(true);
		uint32 microseconds = cycleClock.Microseconds(System::GetCoreTick(), System::GetCoreClock() / 1000000U);
		System::CriticalSection(false);
		return microseconds;
	}


	// The four chains. Each one is a delivery layer over the link, with the framing and the transform its link needs, and each one has the
	// same payload size and the same handlers, with the stack's index in the template argument.
	using UartLinkType = VUB::UartLink<AUART>;
	using CdcLinkType = VUB::CdcLink<SerialPort>;
	using BulkLinkType = VUB::BulkLink<VendorChannel>;
	using HidLinkType = VUB::HidLink<VendorHid, 64>;


	using UartStack = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.OnOutcome<&OutcomeOf<UartIndex>>()
			.OnRestart<&RestartOf<UartIndex>>()
			.Transform<VUB::Crc16Modbus>()
			.Framing<VUB::Cobs>()
			.Link<UartLinkType>()
			.MaxPayload(32)
			.OnReceive<&ReceiveFrom<UartIndex>>()
			.Clock<&MicrosecondClock>();
	}>;


	using CdcStack = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.OnOutcome<&OutcomeOf<CdcIndex>>()
			.OnRestart<&RestartOf<CdcIndex>>()
			.Framing<VUB::Cobs>()
			.Link<CdcLinkType>()
			.MaxPayload(32)
			.OnReceive<&ReceiveFrom<CdcIndex>>()
			.Clock<&MicrosecondClock>();
	}>;


	using BulkStack = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.OnOutcome<&OutcomeOf<BulkIndex>>()
			.OnRestart<&RestartOf<BulkIndex>>()
			.Framing<VUB::LengthPrefix<2>>()
			.Link<BulkLinkType>()
			.MaxPayload(32)
			.OnReceive<&ReceiveFrom<BulkIndex>>()
			.Clock<&MicrosecondClock>();
	}>;


	using HidStack = VUB::Stack<+[] {
		return VUB::Builder()
			.Layer<VUB::Reliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
			.OnOutcome<&OutcomeOf<HidIndex>>()
			.OnRestart<&RestartOf<HidIndex>>()
			.Link<HidLinkType>()
			.MaxPayload(32)
			.OnReceive<&ReceiveFrom<HidIndex>>()
			.Clock<&MicrosecondClock>();
	}>;


	// The critical error handler of System. It writes no text to the line: the message goes to the status block for SWD.
	static void RecordCriticalError(const char* message, const char* file, uint32 line) {
		vubStatus.criticalSet = 1;
		vubStatus.criticalMessage = message;
		vubStatus.criticalFile = file;
		vubStatus.criticalLine = line;
	}


	// The cycle counter that MicrosecondClock reads. System::Init() enables it already; this repeats the setting, so the clock does not
	// depend on the order of the BSP steps.
	static void EnableCycleCounter() {
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	}


	// Session values from the G431's RNG: one 32-bit word per stack, the low 16 bits kept. Returns false when the RNG does not start or
	// does not give a word, and the caller then uses FallbackSessions.
	static bool ReadRandomSessions(std::array<uint16, StackCount>& sessions) {
		LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_RNG);
		static ARNG rng(RNG);
		if (rng.SetParameters({}) != ResultStatus::ok) {
			return false;
		}

		for (std::size_t index = 0; index < sessions.size(); index++) {
			Result<uint32> word = rng.Read();
			if (word.IsErr()) {
				return false;
			}

			sessions[index] = static_cast<uint16>(word.Value());
		}

		return true;
	}


	// The fallback when the RNG fails. The unique ID is the same on every boot, so it is folded to 32 bits and mixed with the cycle counter
	// at the start of Run. That count runs from the reset and depends on the boot path, so two boots can give the same values: this
	// source does not meet the delivery layer's rule of a value that differs on every boot. Each stack's index is added, so the four
	// values differ from each other. The status block says which source was used.
	static void FallbackSessions(std::array<uint16, StackCount>& sessions) {
		std::array<uint8, BSP::UniqueIdSize> uniqueId{};
		BSP::ReadUniqueId(uniqueId);
		uint32 idFold = 0;
		for (uint8 byte : uniqueId) {
			idFold = idFold * 31U + byte;
		}

		uint32 cycles = System::GetCoreTick();
		for (std::size_t index = 0; index < sessions.size(); index++) {
			sessions[index] = static_cast<uint16>((idFold ^ cycles) + static_cast<uint32>(index) * 0x9E37U);
		}
	}


	// Gives the device its serial number string from the unique ID. Called once, before the first Start().
	static ResultStatus SetUniqueSerialNumber() {
		std::array<uint8, BSP::UniqueIdSize> uniqueId{};
		BSP::ReadUniqueId(uniqueId);

		ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
		if (status != ResultStatus::ok) {
			return status;
		}

		return usbDevice.SetSerialNumber(serialDescriptor);
	}


	// Sends the echoes the stack takes now. A busy stack keeps them for the next pass. Any other refusal is counted and the echo is dropped.
	template<typename StackType>
	static void FlushEcho(StackType& stack, StackRecord& record) {
		while (record.echo.Count() > 0) {
			ResultStatus status = stack.Send(record.echo.Front());
			if (status == ResultStatus::busy) {
				return;
			}

			record.lastSendStatus = static_cast<uint32>(status);
			if (status == ResultStatus::ok) {
				record.echoed++;
			} else {
				record.echoRefused++;
			}

			record.echo.Pop();
		}
	}


	// Copies one stack's counters and drop counters into the status block.
	template<typename StackType>
	static void CopyStack(const StackType& stack, const StackRecord& record, volatile VubStackStatus& status) {
		VUB::DropCounters drops = stack.Drops();
		status.session = record.session;
		status.received = record.received;
		status.echoed = record.echoed;
		status.echoRefused = record.echoRefused;
		status.echoDropped = record.echoDropped;
		status.outcomesOk = record.outcomesOk;
		status.outcomesFailed = record.outcomesFailed;
		status.restarts = record.restarts;
		status.lastSendStatus = record.lastSendStatus;
		status.lastOutcomeStatus = record.lastOutcomeStatus;
		status.drops.ringFull = drops.ringFull;
		status.drops.queueFull = drops.queueFull;
		status.drops.tooLong = drops.tooLong;
		status.drops.framingError = drops.framingError;
		status.drops.transformError = drops.transformError;
		status.drops.layerError = drops.layerError;
		status.drops.sendError = drops.sendError;
		status.drops.linkDown = drops.linkDown;
	}


	void Run(ResultStatus consoleStatus) {
		vubStatus.magic = StatusMagic;
		vubStatus.stage = StageStarted;
		vubStatus.consoleResult = static_cast<uint32>(consoleStatus);

		// USART1 carries the UART stack, so System::console and the critical error text are not sent to it. Before the stacks start, the
		// write handler is cleared, and a critical error is kept in the status block.
		System::SetWriteHandler(nullptr);
		System::criticalErrorHandle = RecordCriticalError;
		EnableCycleCounter();

		AUART::Parameters parameters{};
		parameters.baudRate = LineBaud;
		parameters.dataBits = AUART::DataBits::B8;
		parameters.parity = AUART::Parity::None;
		parameters.stopBits = AUART::StopBits::B1;
		vubStatus.stage = StageSerial;
		ResultStatus serialStatus = BSP::consoleSerial.SetParameters(parameters);
		vubStatus.serialResult = static_cast<uint32>(serialStatus);

		vubStatus.stage = StageSession;
		std::array<uint16, StackCount> sessions{};
		if (ReadRandomSessions(sessions)) {
			vubStatus.sessionSource = SessionFromRandom;
		} else {
			FallbackSessions(sessions);
			vubStatus.sessionSource = SessionFromFallback;
		}

		// The links and stacks are static, so their RAM is in .bss and the map shows it, not the main stack.
		static UartLinkType uartLink(BSP::consoleSerial);
		static CdcLinkType cdcLink(serialFunction);
		static BulkLinkType bulkLink(vendorFunction);
		static HidLinkType hidLink(hidFunction);
		static UartStack uartStack(uartLink);
		static CdcStack cdcStack(cdcLink);
		static BulkStack bulkStack(bulkLink);
		static HidStack hidStack(hidLink);
		uartStack.Find<VUB::Reliable>().SetSession(sessions[UartIndex]);
		cdcStack.Find<VUB::Reliable>().SetSession(sessions[CdcIndex]);
		bulkStack.Find<VUB::Reliable>().SetSession(sessions[BulkIndex]);
		hidStack.Find<VUB::Reliable>().SetSession(sessions[HidIndex]);
		for (std::size_t index = 0; index < StackCount; index++) {
			records[index].session = sessions[index];
		}

		vubStatus.stage = StageUsb;
		// A serial number that fails leaves the device without one; the device still enumerates, so the result is not checked here.
		static_cast<void>(SetUniqueSerialNumber());

		USBDeviceAdapter::Parameters usbParameters;
		usbParameters.endpointCount = EndpointCount;
		vubStatus.usbResult = static_cast<uint32>(usbDevice.Start(usbParameters));

		vubStatus.stage = StageServing;
		uint64 nextStatusMs = System::GetMs();
		while (true) {
			usbDevice.Process();
			uartStack.Process();
			cdcStack.Process();
			bulkStack.Process();
			hidStack.Process();
			FlushEcho(uartStack, records[UartIndex]);
			FlushEcho(cdcStack, records[CdcIndex]);
			FlushEcho(bulkStack, records[BulkIndex]);
			FlushEcho(hidStack, records[HidIndex]);

			if (System::GetMs() >= nextStatusMs) {
				nextStatusMs += StatusIntervalMs;
				VUB::UartErrorCounts uartErrors = uartLink.Errors();
				vubStatus.uartErrors.parity = uartErrors.parity;
				vubStatus.uartErrors.noise = uartErrors.noise;
				vubStatus.uartErrors.frame = uartErrors.frame;
				vubStatus.uartErrors.overrun = uartErrors.overrun;
				CopyStack(uartStack, records[UartIndex], vubStatus.uart);
				CopyStack(cdcStack, records[CdcIndex], vubStatus.cdc);
				CopyStack(bulkStack, records[BulkIndex], vubStatus.bulk);
				CopyStack(hidStack, records[HidIndex], vubStatus.hid);
			}
		}
	}
}
