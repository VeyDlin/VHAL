#pragma once
#include "IAdapter.h"
#include <array>

#define VHAL_CAN_ADAPTER


// CAN controller: classic CAN 2.0A/B and, where the hardware has it, CAN FD (STM32 bxCAN and FDCAN, ESP32 TWAI).
//
// Timing: the port derives the bit timing from the kernel clock given to the constructor, the bitrate and the sample
// point. A bitrate that the clock cannot reach within the tolerance of the CAN standard is refused with invalidParameter.
//
// Reception: frames that pass a filter go to a hardware FIFO. With onReceive assigned, the interrupt reads every frame and
// raises onReceive, and Receive() finds nothing. Without it, frames stay in the FIFO for Receive(); when the FIFO is full
// the hardware drops new frames and the port raises onError(Error::ReceiveOverrun).
//
// Transmission: Send() places a frame in a free transmit slot (mailbox or FIFO element) and returns at once.
//
// Interrupts: IrqHandler() serves every interrupt line of the controller. The application calls it from each vector the
// controller has (bxCAN: TX, RX0, RX1, SCE; FDCAN: IT0, IT1; TWAI: one).
template<typename HandleType>
class CANAdapter : public IAdapter {
public:
	enum class Mode : uint8 {
		Normal,
		// Receives and acknowledges nothing, sends nothing: bus monitoring.
		ListenOnly,
		// Transmitted frames are received back internally; the TX pin stays recessive and the bus is not touched. For tests
		// without a transceiver.
		InternalLoopback,
		// As InternalLoopback, but the frames are also driven onto the TX pin.
		ExternalLoopback
	};

	enum class BusState : uint8 {
		Stopped,
		ErrorActive,
		ErrorPassive,
		BusOff
	};

	enum class Error : uint8 {
		Stuff,
		Form,
		Acknowledge,
		BitRecessive,
		BitDominant,
		Crc,
		// A frame passed a filter but the receive FIFO was full; the frame is lost.
		ReceiveOverrun,
		// A frame was not sent: arbitration lost with automatic retransmission off, or an error during transmission.
		TransmitFailed,
		Unknown
	};

	static constexpr uint8 ClassicMaxDataLength = 8;
	static constexpr uint8 FdMaxDataLength = 64;

	struct Frame {
		// 11-bit identifier when extended is false, 29-bit when it is true.
		uint32 id = 0;
		bool extended = false;
		// Remote transmission request (classic CAN only; CAN FD has no remote frames).
		bool remote = false;
		// CAN FD frame. Needs Parameters::dataBitrate above 0.
		bool fd = false;
		// CAN FD only: the data phase runs at Parameters::dataBitrate.
		bool bitRateSwitch = false;
		// Bytes in data. Classic: 0 to 8. CAN FD: 0 to 8, 12, 16, 20, 24, 32, 48 or 64. Send() refuses other lengths with
		// invalidParameter instead of padding them.
		uint8 length = 0;
		std::array<uint8, FdMaxDataLength> data = {};
		// Receive time in the port's timestamp unit (bit times on STM32), 0 when the port has none.
		uint32 timestamp = 0;
	};

	// Accepts a frame when (frame id & mask) == (id & mask) and the identifier type matches.
	struct Filter {
		uint32 id = 0;
		uint32 mask = 0;
		bool extended = false;
	};

	struct ErrorCounters {
		uint8 transmit = 0;
		uint8 receive = 0;
	};

	struct Parameters {
		Mode mode = Mode::Normal;
		// Arbitration phase (and the whole frame for classic CAN), bits per second.
		uint32 nominalBitrate = 500000;
		// CAN FD data phase, bits per second. 0 means classic CAN only: fd frames are refused by Send().
		uint32 dataBitrate = 0;
		// Sample point of the nominal and data bit, in tenths of a percent: 875 is 87.5 %.
		uint16 samplePointPermille = 875;
		// Resend a frame that lost arbitration or met an error, until it is sent or aborted.
		bool automaticRetransmission = true;
		// Leave bus-off by itself after 128 x 11 recessive bits. When false the application calls RecoverFromBusOff().
		bool automaticBusOffRecovery = true;
	};

	// Interrupt context. The frame is valid only during the call.
	std::function<void(const Frame& frame)> onReceive;
	// A frame placed by Send() has been sent (acknowledged by another node).
	std::function<void()> onTransmitComplete;
	std::function<void(BusState state)> onBusStateChanged;
	std::function<void(Error error)> onError;


protected:
	HandleType *canHandle = nullptr;
	uint32 inputBusClockHz = 0;
	Parameters parameters;


public:
	CANAdapter() = default;


	CANAdapter(HandleType *can, uint32 kernelClockHz) : canHandle(can), inputBusClockHz(kernelClockHz) {
	}


	// Stops the controller, applies the parameters and the timing, and leaves it stopped. Filters set before are kept
	// where the hardware keeps them; set them again after SetParameters() to be sure.
	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	// Joins the bus (after 11 recessive bits) with the current parameters and filters.
	virtual ResultStatus Start() = 0;


	// Leaves the bus. Frames waiting in the transmit slots are aborted.
	virtual ResultStatus Stop() = 0;


	// Sets acceptance filter index. Returns outOfRange for an index the hardware does not have (GetFilterCount()), and
	// busy while started on a controller that only takes filters when stopped. With no filter set, the port accepts
	// nothing, so an application that wants every frame sets one filter with mask 0.
	virtual ResultStatus SetFilter(uint8 index, const Filter& filter) = 0;


	virtual ResultStatus ClearFilter(uint8 index) = 0;


	// Number of filters for one identifier type.
	virtual uint8 GetFilterCount(bool extended) const = 0;


	// Places the frame in a free transmit slot and returns. busy when every slot is taken, invalidParameter for a length
	// or identifier the frame type does not allow or an fd frame without a data bitrate, notReady when not started.
	// Callable from interrupt context.
	virtual ResultStatus Send(const Frame& frame) = 0;


	// True when Send() would find a free slot.
	virtual bool CanSend() const = 0;


	// Takes the oldest frame from the receive FIFO. notAvailable when it is empty (always so while onReceive is
	// assigned).
	virtual Result<Frame> Receive() = 0;


	virtual BusState GetBusState() const = 0;


	virtual ErrorCounters GetErrorCounters() const = 0;


	// Starts the bus-off recovery when Parameters::automaticBusOffRecovery is false. notReady unless in BusOff.
	virtual ResultStatus RecoverFromBusOff() = 0;


	virtual void IrqHandler() = 0;


protected:
	// Applies the parameters with the controller stopped: mode, timing from inputBusClockHz, retransmission, bus-off
	// recovery. Calls BeforeInitialization() and AfterInitialization().
	virtual ResultStatus Initialization() = 0;


	virtual inline void CallReceive(const Frame& frame) {
		if (onReceive != nullptr) {
			onReceive(frame);
		}
	}


	virtual inline void CallTransmitComplete() {
		if (onTransmitComplete != nullptr) {
			onTransmitComplete();
		}
	}


	virtual inline void CallBusStateChanged(BusState state) {
		if (onBusStateChanged != nullptr) {
			onBusStateChanged(state);
		}
	}


	virtual inline void CallError(Error error) {
		if (onError != nullptr) {
			onError(error);
		}
	}
};
