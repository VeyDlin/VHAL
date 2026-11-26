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


#if defined(VHAL_VUB_REQUESTS)
// The requests mode's counters: the device's counters of each command and the sweep state. They follow the echo words of the block, so
// the offsets of the echo words are the same in both modes.
struct VubRequestsStatus {
	uint32 sets;
	uint32 sweeps;
	uint32 kicks;
	uint32 nudges;
	uint32 hangs;
	uint32 interims;
	uint32 interimBusy;
	uint32 sweepsExpired;
	uint32 sweepsActive;
	uint32 unclaimed;
	uint32 cancels;
	uint32 notes;
	uint32 eventBusy;
};
#endif


// The volatile status block for SWD. The magic word tells the reader that the block is set. The stage is the last start-up step that was
// reached, so a hang inside a step is visible. The critical fields are written by the critical error handler just before the MCU halts,
// for explicit CriticalError calls only: SystemAssert and System::Abort() do not call the handler. The requests words come last, and only
// in the requests mode (VHAL_VUB_REQUESTS).
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
#if defined(VHAL_VUB_REQUESTS)
	VubRequestsStatus requests;
#endif
};


volatile VubStatusBlock vubStatus __attribute__((used)) = {};
