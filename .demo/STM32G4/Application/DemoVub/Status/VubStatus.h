#pragma once
#include <Utilities/DataTypes.h>


// The drop counters of one stack, as Drops() reports them.
struct VubDropStatus {
	uint32 ringFull;
	uint32 queueFull;
	uint32 tooLong;
	uint32 framingError;
	uint32 transformError;
	uint32 layerError;
	uint32 sendError;
	uint32 linkDown;
};


// The receive errors of the UART link, as UartLink::Errors() counts them.
struct VubUartErrorStatus {
	uint32 parity;
	uint32 noise;
	uint32 frame;
	uint32 overrun;
};


// One stack. received counts the payloads the handler took, echoed the payloads sent back, echoRefused the echoes the stack refused
// with an error other than busy, and echoDropped the payloads the echo queue could not keep. The outcomes are the results the
// delivery layer reports for the echoes this device sent.
struct VubStackStatus {
	uint32 session;
	uint32 received;
	uint32 echoed;
	uint32 echoRefused;
	uint32 echoDropped;
	uint32 outcomesOk;
	uint32 outcomesFailed;
	uint32 restarts;
	uint32 lastSendStatus;
	uint32 lastOutcomeStatus;
	VubDropStatus drops;
};


// The volatile status block for SWD. The magic word tells the reader that the block is set. The stage is the last start-up step that was
// reached, so a hang inside a step is visible. The critical fields are written by the critical error handler just before the MCU halts,
// for explicit CriticalError calls only: SystemAssert and System::Abort() do not call the handler.
struct VubStatusBlock {
	uint32 magic;
	uint32 stage;
	uint32 consoleResult;
	uint32 serialResult;
	uint32 sessionSource;
	uint32 usbResult;
	VubUartErrorStatus uartErrors;
	VubStackStatus uart;
	VubStackStatus cdc;
	VubStackStatus bulk;
	VubStackStatus hid;
	uint32 criticalSet;
	const char* criticalMessage;
	const char* criticalFile;
	uint32 criticalLine;
};


volatile VubStatusBlock vubStatus __attribute__((used)) = {};
