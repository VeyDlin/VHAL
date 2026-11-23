#include <DemoModbus/DemoModbus.h>
#include <DemoModbus/WrappingClock.h>
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <array>
#include <cstddef>
#include <cstdint>


// The status the SWD reader sees. The main loop writes the fields, and the critical error handler writes the critical fields just
// before the MCU halts. The magic word tells the reader that the block is set. The stage is the last start-up step that was reached,
// so a hang inside a step is visible; Abort() does not reach the critical error handler.
struct ModbusStatusBlock {
	uint32 magic;
	uint32 stage;
	uint32 consoleResult;
	uint32 serialResult;
	uint32 beginResult;
	uint32 framesAnswered;
	uint32 lastStatus;
	uint32 criticalSet;
	const char* criticalMessage;
	const char* criticalFile;
	uint32 criticalLine;
};


volatile ModbusStatusBlock modbusStatus __attribute__((used)) = {};


namespace DemoModbus {
constexpr uint32 StatusMagic = 0x4D424453U;
constexpr uint32 LineBaud = 9600;
constexpr uint8 LineSlaveAddress = 1;
constexpr uint16 RejectedHoldingValue = 0xFFFF;
constexpr uint32 StageStarted = 1;
constexpr uint32 StageSettingSerial = 2;
constexpr uint32 StageBeginning = 3;
constexpr uint32 StageServing = 4;


// The values of the view. Only the main loop reads and writes them, because Process() runs there, so they need no volatile.
static uint16 holdingValue = 0;
static std::array<uint16, 3> holdingBlock{};
static std::array<bool, 8> coils{};
static uint16 goodFrames = 0;


// The write check of holding 0x0000: 0xFFFF is refused, so the write gets exception 03 and the value stays.
static bool CheckHoldingValue(uint16 value) {
	return value != RejectedHoldingValue;
}


// Input register 0x0010: the number of good frames the server has taken in.
static uint16 ReadGoodFrames() {
	return goodFrames;
}


// Discrete inputs 0x0000 to 0x0003: input n is coil n XOR (n is odd).
static std::array<bool, 4> ReadDiscreteValues() {
	std::array<bool, 4> inputs{};
	for (std::size_t index = 0; index < inputs.size(); index++) {
		bool odd = (index & 1U) != 0;
		inputs[index] = coils[index] != odd;
	}

	return inputs;
}


// The microsecond clock of the RTU transport: the DWT cycle counter divided by the core clock in MHz, with the wraps of the counter
// counted (WrappingClock). The receive interrupt reads the clock too, so the read is inside a critical section. Begin reads it twice
// and returns error when it does not advance.
static WrappingClock cycleClock;


static uint32 MicrosecondClock() {
	System::CriticalSection(true);
	uint32 microseconds = cycleClock.Microseconds(System::GetCoreTick(), System::GetCoreClock() / 1000000U);
	System::CriticalSection(false);
	return microseconds;
}


// Holds the MCU in a loop, so the status block stays for SWD. The server is not started.
static void Halt() {
	while (true) {
		System::DelayMs(1000);
	}
}


// The critical error handler of System. It writes no text to the line: the message goes to the status block for SWD.
static void RecordCriticalError(const char* message, const char* file, uint32 line) {
	modbusStatus.criticalSet = 1;
	modbusStatus.criticalMessage = message;
	modbusStatus.criticalFile = file;
	modbusStatus.criticalLine = line;
}


// The cycle counter that MicrosecondClock reads. System::Init() enables it already. This repeats the setting, so the clock does not
// depend on the order of the BSP steps.
static void EnableCycleCounter() {
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}


// The view: holding 0x0000 (one register, read and write, refuses 0xFFFF), holding 0x0001 to 0x0003 (a block, read and write), input
// 0x0010 (the good-frame count), coils 0x0000 to 0x0007 (read and write) and discrete inputs 0x0000 to 0x0003 (derived from the coils).
using DemoView = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&holdingValue, &CheckHoldingValue))
		.HoldingRegisters(0x0001, Variable(&holdingBlock))
		.InputRegisters(0x0010, Computed(&ReadGoodFrames))
		.Coils(0x0000, Variable(&coils))
		.DiscreteInputs(0x0000, Computed(&ReadDiscreteValues));
}>;


// The RTU transport over USART1 without RS-485 pins. The turnaround is two characters after the transmit completes.
using Link = RtuTransport<AUART, AGPIO, +[] {
	return RtuBuilder()
		.Turnaround(2)
		.Clock<&MicrosecondClock>();
}>;


using Server = ModbusServer<Link, RtuFramer, DemoView, +[] {
	return ServerBuilder()
		.SlaveAddress(LineSlaveAddress);
}>;


void Run(ResultStatus consoleStatus) {
	modbusStatus.magic = StatusMagic;
	modbusStatus.stage = StageStarted;
	modbusStatus.consoleResult = static_cast<uint32>(consoleStatus);

	// BSP::Init() routes System::console and the critical error text to USART1, which is the Modbus line in this build. Both are
	// cleared before the server starts, so no text reaches the line. A critical error is kept in modbusStatus instead.
	System::SetWriteHandler(nullptr);
	System::criticalErrorHandle = RecordCriticalError;

	EnableCycleCounter();

	AUART::Parameters parameters{};
	parameters.baudRate = LineBaud;
	parameters.dataBits = AUART::DataBits::B8;
	parameters.parity = AUART::Parity::Even;
	parameters.stopBits = AUART::StopBits::B1;
	modbusStatus.stage = StageSettingSerial;
	ResultStatus serialStatus = BSP::consoleSerial.SetParameters(parameters);
	modbusStatus.serialResult = static_cast<uint32>(serialStatus);
	if (serialStatus != ResultStatus::ok) {
		Halt();
	}

	modbusStatus.stage = StageBeginning;
	Link link(BSP::consoleSerial);
	ResultStatus beginStatus = link.Begin();
	modbusStatus.beginResult = static_cast<uint32>(beginStatus);
	if (beginStatus != ResultStatus::ok) {
		Halt();
	}

	modbusStatus.stage = StageServing;
	Server server(link);
	while (true) {
		ResultStatus status = server.Process();
		if (status == ResultStatus::ok) {
			goodFrames++;
			modbusStatus.framesAnswered = goodFrames;
		}

		if (status != ResultStatus::timeout) {
			modbusStatus.lastStatus = static_cast<uint32>(status);
		}
	}
}
}
