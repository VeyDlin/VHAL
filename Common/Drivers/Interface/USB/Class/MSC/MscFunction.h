#pragma once
#include <Drivers/Interface/USB/Class/MSC/MscProtocol.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Drivers/Memory/BlockDevice/IBlockDevice.h>
#include <System/System.h>
#include <Utilities/VSTD/utility/Delegate.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>


// The identity INQUIRY answers with: the vendor (8 characters), the product (16) and the revision (4), cut or padded with spaces, and
// the removable bit. The text must refer to static storage, because the function keeps the views.
struct MscIdentity {
	std::string_view vendor;
	std::string_view product;
	std::string_view revision;
	bool removable = true;
};


// Mass storage function of one logical unit: the Bulk-Only Transport with its command block and status wrappers, and the SCSI
// commands a host sends to a block device. The medium is an IBlockDevice. BufferSize is the data buffer in bytes. It is a multiple of
// the packet size and of the medium's block size, and it is the largest data stage of one transfer.
//
// Who does what. The endpoint callbacks (interrupt context) take the CBW, decide the command, move the data packets into the buffer
// and send the CSW. Everything that touches the medium runs in Process(), from the application's loop. A read fills the buffer and
// then arms the IN endpoint; a write takes the bytes the OUT endpoint stored and writes them. While Process() works on the medium, the
// endpoint that would carry the next transfer stays unarmed, so the host gets NAK and waits.
//
// The Bulk-Only rules follow BOT 1.0. A CBW that is not valid (wrong length or signature) stalls both pipes until a Bulk-Only Mass
// Storage Reset. The thirteen host/device cases of section 6.7 are decided from the CBW and the device's intent, before any data moves:
// the function stalls the pipe that ends a transfer the host and the device disagree about, and sends the CSW after the host has
// cleared the halt. The residue of the CSW is the host's length minus the bytes of the data stage that were moved.
//
// The presence and the write protection of the medium are read in Process() while the function is idle, because IBlockDevice calls may
// block. The interrupt uses the values read last, and the constructor reads them once. A configuration arms no CBW until Process() has
// read the presence after it, so a TEST UNIT READY is never decided from the presence of an earlier configuration. An eject calls the
// eject delegate and reports the medium as not present at once. The delegate is expected to make IBlockDevice::IsPresent() return
// notExist, so the next Process() agrees.
//
// Context: the function keeps no pointer to the device. Process() takes critical sections around the state it shares with the
// interrupt, and never around the medium access.
template<std::size_t BufferSize>
class MscFunction : public USBFunction {
	static_assert(BufferSize >= MscPacketSize && BufferSize % MscPacketSize == 0, "the data buffer is a whole number of packets, at least one");


public:
	// Called from the interrupt when the host ejects the medium: START STOP UNIT with LOEJ set and START clear. Non-allocating.
	using EjectDelegate = VSTD::Delegate<void()>;


	// The lengths of the fixed answers. The data buffer holds all of them.
	static constexpr std::size_t InquiryLength = 36;
	static constexpr std::size_t RequestSenseLength = 18;
	static constexpr std::size_t ReadFormatCapacitiesLength = 12;
	static constexpr std::size_t ModeSense6Length = 4;
	static constexpr std::size_t ModeSense10Length = 8;
	static constexpr std::size_t ReadCapacityLength = 8;


private:
	// Where the host is in the transaction.
	enum class Stage : uint8 {
		// Waiting for a CBW.
		Idle,
		// A valid CBW was accepted. The command runs until its CSW has been sent.
		Command,
		// A CBW was not valid. Both pipes stay stalled until a Bulk-Only Mass Storage Reset.
		Invalid
	};


	// The direction the device intends to move data in the data stage.
	enum class Direction : uint8 {
		None,
		In,
		Out
	};


	// The work on the medium that the command still needs. Process() does it.
	enum class Medium : uint8 {
		None,
		Read,
		Write,
		Verify,
		Sync
	};


	// The transfer the OUT endpoint is armed for: the CBW, or the data of a write.
	enum class OutTarget : uint8 {
		Cbw,
		Data
	};


	// The transfer the IN endpoint is armed for: the data of a read or an answer, or the CSW.
	enum class InTarget : uint8 {
		Data,
		Csw
	};


	// The command in progress, from its CBW to its CSW.
	struct Command {
		uint32 tag = 0;
		// dCBWDataTransferLength: what the host expects.
		uint32 hostLength = 0;
		bool hostIn = false;
		// What the device intends: its direction and its length.
		Direction deviceDirection = Direction::None;
		uint32 deviceLength = 0;
		// The bytes of the data stage: the smaller of the two lengths, or the host's when the device intends more.
		uint32 expected = 0;
		// Bytes of the data stage moved so far.
		uint32 transferred = 0;
		uint8 status = MscStatusPassed;
		// Case 5: the device sends less than the host expects, so the pipe is stalled after the data.
		bool stallAfterData = false;
		Medium medium = Medium::None;
		// Read and Write: the block of the byte at transferred. Verify: the next block to verify.
		uint32 firstBlock = 0;
		// Verify: the blocks still to verify.
		uint32 blocks = 0;
		// The bytes of the current stage in the buffer.
		uint32 chunkBytes = 0;
		// Write: the transfer offset of the chunk in the buffer.
		uint32 chunkStart = 0;
	};


	static constexpr uint32 BufferBytes = static_cast<uint32>(BufferSize);


	IBlockDevice& blockDevice;
	MscIdentity identity;
	EjectDelegate ejectDelegate;
	uint8 inAddress = 0;
	uint8 outAddress = 0;
	uint32 blockSize = 0;
	uint32 blockCount = 0;
	bool configured = false;
	Stage stage = Stage::Idle;
	bool inArmed = false;
	InTarget inTarget = InTarget::Csw;
	bool inHalted = false;
	bool outArmed = false;
	OutTarget outTarget = OutTarget::Cbw;
	bool outHalted = false;
	bool cswPending = false;
	bool mediumPending = false;
	// Set from the plan of a medium access to its commit. No CBW is armed meanwhile, so the next command cannot write the buffer
	// the access is using.
	bool mediumBusy = false;
	bool presentCache = false;
	bool readOnlyCache = false;
	// False from a configuration until Process() has read the presence after it. No CBW is armed meanwhile.
	bool presenceFresh = false;
	bool preventRemoval = false;
	// Counts the ejects, so a presence sample that was taken across an eject is dropped.
	uint32 ejectCount = 0;
	// Changes at each reset, so a Process() that began before the reset drops its medium work.
	uint32 generation = 0;
	MscSense sense;
	Command command;
	std::array<uint8, MscPacketSize> cbw{};
	std::array<uint8, MscCswLength> csw{};
	std::array<uint8, 1> maxLun{};
	std::array<uint8, BufferSize> data{};


public:
	MscFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints, IBlockDevice& device, const MscIdentity& deviceIdentity)
		: USBFunction(interfaces, endpoints),
		  blockDevice(device),
		  identity(deviceIdentity) {
		blockSize = blockDevice.GetBlockSize();
		blockCount = blockDevice.GetBlockCount();
		presentCache = blockDevice.IsPresent() == ResultStatus::ok;
		readOnlyCache = blockDevice.IsReadOnly() == ResultStatus::readOnly;
	}


	// From the fluent builder's layout (USBDescriptors::Function()).
	MscFunction(const USBFunctionLayout& layout, IBlockDevice& device, const MscIdentity& deviceIdentity)
		: MscFunction(layout.interfaces, layout.endpoints, device, deviceIdentity) {
	}


	// The buffers are members that the spans and the transfers refer to, so the function is not copied or moved.
	MscFunction(const MscFunction&) = delete;


	MscFunction& operator=(const MscFunction&) = delete;


	MscFunction(MscFunction&&) = delete;


	MscFunction& operator=(MscFunction&&) = delete;


	// Set before the device starts: busy once the function is configured.
	ResultStatus SetEjectDelegate(EjectDelegate value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		ejectDelegate = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Reads the medium's presence and write protection while the function is idle, then does the medium work the current command still
	// needs. One block chunk per call: a read or write moves at most BufferSize bytes, and the next chunk waits for the next call.
	void Process(USBDeviceContext& context) override {
		RefreshMedium();

		// Arms the CBW that a configuration held back until the presence was read. It does nothing when a CBW is armed already.
		System::CriticalSection(true);
		ArmCbw(context);
		if (!configured || !mediumPending) {
			System::CriticalSection(false);
			return;
		}

		mediumPending = false;
		mediumBusy = true;
		uint32 session = generation;
		Medium work = command.medium;
		uint32 firstIndex = 0;
		uint32 byteCount = 0;
		PlanMedium(work, firstIndex, byteCount);
		System::CriticalSection(false);

		ResultStatus status = MediumAccess(work, firstIndex, byteCount);

		System::CriticalSection(true);
		mediumBusy = false;
		if (session == generation) {
			CommitMedium(context, work, status);
		}

		// A CBW that a reset held back during the access is armed now. It does nothing when the commit has started a command.
		ArmCbw(context);
		System::CriticalSection(false);
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		ResultStatus layout = CheckLayout();
		if (layout != ResultStatus::ok) {
			return layout;
		}

		ResetSession();
		ResultStatus status = OpenPipe(context, inAddress);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = OpenPipe(context, outAddress);
		if (status != ResultStatus::ok) {
			context.CloseEndpoint(inAddress);
			return status;
		}

		// The CBW is armed by Process(), once the presence has been read for this configuration.
		configured = true;
		return ResultStatus::ok;
	}


	// Called from Stop() inside the caller's critical section, and from SET_CONFIGURATION(0). No section is entered here.
	void OnDeconfigured() override {
		configured = false;
		ResetSession();
	}


	void OnBusReset() override {
		configured = false;
		ResetSession();
	}


	// The class requests: GET_MAX_LUN answers 0, and the Bulk-Only Mass Storage Reset returns the function to its idle stage. Each one
	// must name the function's interface in wIndex, with wValue 0 and the wLength of its data stage (1 for GET_MAX_LUN, 0 for the reset).
	// Anything else on the interface is not supported.
	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		if (setup.recipient != USBRecipient::Interface || setup.type != USBRequestType::Class || setup.value != 0 || !IsOwnInterface(setup.index)) {
			return ResultStatus::notSupported;
		}

		if (setup.request == MscRequestGetMaxLun) {
			if (setup.direction != USBDirection::In || setup.length != 1) {
				return ResultStatus::notSupported;
			}

			maxLun[0] = 0;
			return context.ControlSendData(maxLun);
		}

		if (setup.request == MscRequestBulkOnlyReset) {
			if (setup.direction != USBDirection::Out || setup.length != 0) {
				return ResultStatus::notSupported;
			}

			BulkOnlyReset(context);
			return ResultStatus::ok;
		}

		return ResultStatus::notSupported;
	}


	// The CBW, the data of a write, the data of a read or an answer, and the CSW each come here when their transfer completes.
	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			if (!inArmed) {
				return;
			}

			inArmed = false;
			if (inTarget == InTarget::Csw) {
				CompleteStatus(context);
				return;
			}

			HandleDataIn(context, byteCount);
			return;
		}

		if (address == outAddress) {
			if (!outArmed) {
				return;
			}

			outArmed = false;
			if (outTarget == OutTarget::Cbw) {
				HandleCbw(context, byteCount);
				return;
			}

			HandleDataOut(context, byteCount);
		}
	}


	// A halt set by the device, or cleared by the host with CLEAR_FEATURE(ENDPOINT_HALT). A clear of the IN halt sends a CSW that waited for
	// it, and a clear of the OUT halt arms the CBW once the device is idle.
	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			inHalted = halted;
			if (halted) {
				inArmed = false;
				return;
			}

			// BOT 6.6.1 keeps the pipes stalled until a reset, so a clear by the host alone does not reopen the pipe.
			if (stage == Stage::Invalid) {
				StallIn(context);
				return;
			}

			TrySendCsw(context);
			return;
		}

		if (address == outAddress) {
			outHalted = halted;
			if (halted) {
				outArmed = false;
				return;
			}

			if (stage == Stage::Invalid) {
				StallOut(context);
				return;
			}

			ArmCbw(context);
		}
	}


private:
	// The layout must be one interface with one IN and one OUT bulk endpoint, and the buffer must hold whole blocks.
	ResultStatus CheckLayout() {
		if (GetInterfaces().size() != 1) {
			return ResultStatus::invalidParameter;
		}

		if (blockSize == 0 || blockCount == 0 || BufferSize % blockSize != 0) {
			return ResultStatus::invalidParameter;
		}

		bool inFound = false;
		bool outFound = false;
		for (uint8 address : GetEndpoints()) {
			if (!USBEndpoint::IsValid(address) || USBEndpoint::Number(address) == 0) {
				return ResultStatus::invalidParameter;
			}

			if (USBEndpoint::IsIn(address)) {
				if (inFound) {
					return ResultStatus::invalidParameter;
				}

				inFound = true;
				inAddress = address;
			} else {
				if (outFound) {
					return ResultStatus::invalidParameter;
				}

				outFound = true;
				outAddress = address;
			}
		}

		return inFound && outFound ? ResultStatus::ok : ResultStatus::invalidParameter;
	}


	static ResultStatus OpenPipe(USBDeviceContext& context, uint8 address) {
		USBDeviceAdapter::EndpointConfig config;
		config.address = address;
		config.type = USBEndpointType::Bulk;
		config.maxPacketSize = MscPacketSize;
		config.doubleBuffer = false;
		return context.OpenEndpoint(config);
	}


	// Clears the state of the transfers and the command. The halts stay as they are: the host clears them in the reset recovery.
	void ResetTransport() {
		stage = Stage::Idle;
		cswPending = false;
		mediumPending = false;
		outTarget = OutTarget::Cbw;
		generation++;
		sense = MscSenseNone;
		command = Command();
	}


	// Clears everything, for a new configuration, a bus reset or a deconfiguration. The endpoints are closed by the core, so no halt
	// survives them.
	void ResetSession() {
		ResetTransport();
		inArmed = false;
		inHalted = false;
		outArmed = false;
		outHalted = false;
		preventRemoval = false;
		presenceFresh = false;
	}


	// The Bulk-Only Mass Storage Reset: the device drops the command it holds and waits for a CBW. A transfer the device has armed is
	// abandoned by stalling its pipe, because the host clears the halt next in the reset recovery.
	void BulkOnlyReset(USBDeviceContext& context) {
		if (inArmed) {
			StallIn(context);
		}

		if (outArmed && outTarget == OutTarget::Data) {
			StallOut(context);
		}

		ResetTransport();
		ArmCbw(context);
	}


	// Arms the OUT endpoint for a CBW when the device is idle and the pipe is open. Returns ok when nothing needed doing. Nothing is
	// armed during a medium access: Process() arms the CBW once the access is over. Nothing is armed before the presence of the
	// configuration has been read, either: Process() arms it then.
	ResultStatus ArmCbw(USBDeviceContext& context) {
		if (!configured || !presenceFresh || stage != Stage::Idle || mediumBusy || outArmed || outHalted) {
			return ResultStatus::ok;
		}

		outArmed = true;
		outTarget = OutTarget::Cbw;
		ResultStatus status = context.StartOutTransfer(outAddress, cbw);
		if (status != ResultStatus::ok) {
			outArmed = false;
		}

		return status;
	}


	// Arms the IN endpoint with the first bytes of the buffer. Returns the adapter's status; on failure nothing is armed.
	ResultStatus ArmIn(USBDeviceContext& context, uint32 bytes) {
		inArmed = true;
		inTarget = InTarget::Data;
		ResultStatus status = context.StartInTransfer(inAddress, std::span<const uint8>(data.data(), bytes), false);
		if (status != ResultStatus::ok) {
			inArmed = false;
		}

		return status;
	}


	// Arms the OUT endpoint for the next chunk of a write: at most BufferSize bytes, as many as the data stage still needs.
	ResultStatus ArmDataOut(USBDeviceContext& context) {
		uint32 remaining = command.expected - command.transferred;
		uint32 chunk = std::min<uint32>(BufferBytes, remaining);
		command.chunkBytes = chunk;
		outArmed = true;
		outTarget = OutTarget::Data;
		ResultStatus status = context.StartOutTransfer(outAddress, std::span<uint8>(data.data(), chunk));
		if (status != ResultStatus::ok) {
			outArmed = false;
		}

		return status;
	}


	void StallIn(USBDeviceContext& context) {
		inArmed = false;
		static_cast<void>(context.Stall(inAddress));
		inHalted = true;
	}


	void StallOut(USBDeviceContext& context) {
		outArmed = false;
		static_cast<void>(context.Stall(outAddress));
		outHalted = true;
	}


	// A CBW that is not valid stalls both pipes. The stage stays Invalid until a reset, so no further CBW is taken.
	void Invalid(USBDeviceContext& context) {
		stage = Stage::Invalid;
		cswPending = false;
		mediumPending = false;
		StallIn(context);
		StallOut(context);
	}


	// The command is finished: its CSW is sent once the IN pipe is free and the medium has no work left.
	void Finish(USBDeviceContext& context) {
		cswPending = true;
		TrySendCsw(context);
	}


	// Sends the CSW when it is pending and the IN pipe is idle, not halted, and no medium work is left. Otherwise the CSW waits for the
	// event that frees it: the end of a transfer, the clear of the halt, or the end of the medium work.
	void TrySendCsw(USBDeviceContext& context) {
		if (!configured || !cswPending || mediumPending || inHalted || inArmed || stage == Stage::Invalid) {
			return;
		}

		uint32 residue = command.transferred <= command.hostLength ? command.hostLength - command.transferred : 0;
		StoreU32Le(csw, 0, MscCswSignature);
		StoreU32Le(csw, 4, command.tag);
		StoreU32Le(csw, 8, residue);
		csw[12] = command.status;

		cswPending = false;
		inArmed = true;
		inTarget = InTarget::Csw;
		ResultStatus status = context.StartInTransfer(inAddress, csw, false);
		if (status != ResultStatus::ok) {
			inArmed = false;
			cswPending = true;
		}
	}


	// The CSW went out: the command is over, and the next CBW is armed.
	void CompleteStatus(USBDeviceContext& context) {
		stage = Stage::Idle;
		ArmCbw(context);
	}


	// Takes a CBW: checks it, reads the command block, decides the command and starts its data stage.
	void HandleCbw(USBDeviceContext& context, uint32 byteCount) {
		std::span<const uint8> wrapper(cbw.data(), MscPacketSize);
		if (byteCount != MscCbwLength || LoadU32Le(wrapper, 0) != MscCbwSignature) {
			Invalid(context);
			return;
		}

		uint8 flags = cbw[12];
		uint8 lun = cbw[13];
		uint8 commandLength = cbw[14];

		// A CBW that is not meaningful (reserved bits, another LUN, a command length outside 1 to 16) is answered as a failed command
		// with no data of the device's own. BOT leaves the response unspecified, and this is the answer that keeps the host in step.
		bool meaningful = (flags & 0x7F) == 0 && lun == 0 && (commandLength & 0xE0) == 0 && commandLength >= 1 && commandLength <= MscCommandLength;

		command = Command();
		command.tag = LoadU32Le(wrapper, 4);
		command.hostLength = LoadU32Le(wrapper, 8);
		command.hostIn = (flags & MscFlagDataIn) != 0;
		stage = Stage::Command;

		if (meaningful) {
			std::array<uint8, MscCommandLength> block{};
			std::copy_n(cbw.begin() + MscCbwCommandOffset, commandLength, block.begin());
			Decode(block, commandLength);
		} else {
			Refuse(MscSenseInvalidField);
		}

		Dispatch(context);
	}


	// The first decision of a command: the case of the BOT matrix, from the host's length and direction and the device's intent.
	void Dispatch(USBDeviceContext& context) {
		if (command.hostLength == 0) {
			if (command.deviceDirection == Direction::None) {
				// Case 1 (Hn = Dn): nothing to move. Only medium work may remain.
				if (command.medium != Medium::None) {
					mediumPending = true;
					return;
				}

				Finish(context);
				return;
			}

			// Cases 2 and 3 (Hn < Di, Hn < Do): the device has data the host did not ask for. A phase error, with no data moved.
			command.status = MscStatusPhaseError;
			command.medium = Medium::None;
			Finish(context);
			return;
		}

		if (command.hostIn) {
			DispatchHostIn(context);
			return;
		}

		DispatchHostOut(context);
	}


	// Host expects data from the device (Hi): cases 4 to 8.
	void DispatchHostIn(USBDeviceContext& context) {
		if (command.deviceDirection == Direction::None) {
			// Case 4 (Hi > Dn): the device has no data. The IN pipe is stalled and the CSW follows the clear.
			StallIn(context);
			FinishOrWaitForMedium(context);
			return;
		}

		if (command.deviceDirection == Direction::Out) {
			// Case 8 (Hi <> Do): the device intends to receive where the host expects to send to it. A phase error.
			command.status = MscStatusPhaseError;
			command.medium = Medium::None;
			StallIn(context);
			Finish(context);
			return;
		}

		if (command.deviceLength < command.hostLength) {
			// Case 5 (Hi > Di): the device sends what it has, then stalls the pipe so the host stops asking.
			command.expected = command.deviceLength;
			command.stallAfterData = true;
		} else if (command.deviceLength == command.hostLength) {
			// Case 6 (Hi = Di): the data is exactly the host's length.
			command.expected = command.hostLength;
		} else {
			// Case 7 (Hi < Di): the device sends the host's length and reports a phase error.
			command.expected = command.hostLength;
			command.status = MscStatusPhaseError;
		}

		StartDataIn(context);
	}


	// Host sends data to the device (Ho): cases 9 to 13.
	void DispatchHostOut(USBDeviceContext& context) {
		if (command.deviceDirection == Direction::None) {
			// Case 9 (Ho > Dn): the device takes none of the data. The OUT pipe is stalled; the CSW carries the whole length as residue.
			StallOut(context);
			FinishOrWaitForMedium(context);
			return;
		}

		if (command.deviceDirection == Direction::In || command.deviceLength > command.hostLength) {
			// Case 10 (Ho <> Di) and case 13 (Ho < Do): the device would send, or would take more than the host sends. A phase error,
			// and the OUT pipe is stalled at once, which the BOT allows.
			command.status = MscStatusPhaseError;
			command.medium = Medium::None;
			StallOut(context);
			Finish(context);
			return;
		}

		// Cases 11 and 12 (Ho > Do, Ho = Do): the device takes its length. In case 11 the pipe is stalled after the last chunk.
		command.expected = command.deviceLength;
		if (ArmDataOut(context) != ResultStatus::ok) {
			command.status = MscStatusPhaseError;
			command.medium = Medium::None;
			StallOut(context);
			Finish(context);
		}
	}


	void FinishOrWaitForMedium(USBDeviceContext& context) {
		if (command.medium != Medium::None) {
			mediumPending = true;
			return;
		}

		Finish(context);
	}


	// Starts the data stage of a device-to-host command. A read gets its first chunk from Process(); an answer is already in the buffer.
	void StartDataIn(USBDeviceContext& context) {
		if (command.medium == Medium::Read) {
			mediumPending = true;
			return;
		}

		command.chunkBytes = command.expected;
		if (ArmIn(context, command.expected) != ResultStatus::ok) {
			command.status = MscStatusPhaseError;
			command.medium = Medium::None;
			StallIn(context);
			Finish(context);
		}
	}


	// An IN transfer of the data stage completed. The stage ends when the expected bytes have gone.
	void HandleDataIn(USBDeviceContext& context, uint32 byteCount) {
		command.transferred += byteCount;
		if (command.transferred < command.expected) {
			if (command.medium == Medium::Read) {
				mediumPending = true;
				return;
			}

			command.status = MscStatusPhaseError;
			command.medium = Medium::None;
			StallIn(context);
			Finish(context);
			return;
		}

		if (command.stallAfterData) {
			StallIn(context);
		}

		Finish(context);
	}


	// An OUT chunk of a write arrived. Process() writes it. A short chunk is a phase error, because the host sends whole chunks.
	void HandleDataOut(USBDeviceContext& context, uint32 byteCount) {
		if (byteCount != command.chunkBytes) {
			command.status = MscStatusPhaseError;
			command.medium = Medium::None;
			StallOut(context);
			Finish(context);
			return;
		}

		command.chunkStart = command.transferred;
		command.transferred += byteCount;
		mediumPending = true;

		// Case 11: the device takes its length and then ends the transfer, so the host's next OUT token is stalled.
		if (command.transferred == command.expected && command.deviceLength < command.hostLength) {
			StallOut(context);
		}
	}


	// Decides one command from the CBW's CDB. The answer's bytes go into the buffer, and the medium work is marked for Process().
	void Decode(const std::array<uint8, MscCommandLength>& block, uint8 commandLength) {
		uint8 operation = block[0];
		std::span<const uint8> cdb(block.data(), block.size());

		// The sense data of the last error is kept until the next command, except that REQUEST SENSE reads it without clearing it.
		if (operation != ScsiRequestSense) {
			sense = MscSenseNone;
		}

		// The CBW's command length is shorter than the opcode needs, so the bytes the command reads are padding. The command is refused
		// rather than answered from zeros.
		if (commandLength < RequiredCdbLength(operation)) {
			Refuse(MscSenseInvalidField);
			return;
		}

		if (operation == ScsiTestUnitReady) {
			if (presentCache) {
				Accept();
			} else {
				Refuse(MscSenseNotPresent);
			}

			return;
		}

		if (operation == ScsiRequestSense) {
			DecodeRequestSense(cdb);
			return;
		}

		if (operation == ScsiInquiry) {
			DecodeInquiry(cdb);
			return;
		}

		if (operation == ScsiModeSense6) {
			DecodeModeSense6(cdb);
			return;
		}

		if (operation == ScsiModeSense10) {
			DecodeModeSense10(cdb);
			return;
		}

		if (operation == ScsiStartStopUnit) {
			DecodeStartStopUnit(cdb);
			return;
		}

		if (operation == ScsiPreventAllowMediumRemoval) {
			preventRemoval = (cdb[4] & 0x01) != 0;
			Accept();
			return;
		}

		if (operation == ScsiReadFormatCapacities) {
			DecodeReadFormatCapacities(cdb);
			return;
		}

		if (operation == ScsiReadCapacity10) {
			DecodeReadCapacity();
			return;
		}

		if (operation == ScsiRead10) {
			DecodeMediumTransfer(cdb, false);
			return;
		}

		if (operation == ScsiWrite10) {
			DecodeMediumTransfer(cdb, true);
			return;
		}

		if (operation == ScsiVerify10) {
			DecodeVerify(cdb);
			return;
		}

		if (operation == ScsiSynchronizeCache10) {
			if (presentCache) {
				Accept();
				command.medium = Medium::Sync;
			} else {
				Refuse(MscSenseNotPresent);
			}

			return;
		}

		Refuse(MscSenseInvalidCommand);
	}


	// The command passed with no data of the device's own.
	void Accept() {
		command.status = MscStatusPassed;
		command.deviceDirection = Direction::None;
		command.deviceLength = 0;
		command.medium = Medium::None;
	}


	// The command failed with no data of the device's own. The sense data says why.
	void Refuse(const MscSense& error) {
		sense = error;
		command.status = MscStatusFailed;
		command.deviceDirection = Direction::None;
		command.deviceLength = 0;
		command.medium = Medium::None;
	}


	// The command passes and the device answers with length bytes from the buffer. The host gets at most its own length.
	void Answer(uint32 length) {
		Accept();
		command.deviceDirection = Direction::In;
		command.deviceLength = length;
	}


	void DecodeInquiry(std::span<const uint8> cdb) {
		// EVPD and CMDT (byte 1, bits 0 and 1) select pages this function does not have.
		if ((cdb[1] & 0x03) != 0 || cdb[2] != 0) {
			Refuse(MscSenseInvalidField);
			return;
		}

		std::fill_n(data.begin(), InquiryLength, static_cast<uint8>(0));
		data[0] = 0x00;
		data[1] = identity.removable ? 0x80 : 0x00;
		data[2] = 0x04;
		data[3] = 0x02;
		data[4] = static_cast<uint8>(InquiryLength - 5);
		CopyPadded(std::span<uint8>(data.data() + 8, 8), identity.vendor);
		CopyPadded(std::span<uint8>(data.data() + 16, 16), identity.product);
		CopyPadded(std::span<uint8>(data.data() + 32, 4), identity.revision);
		Answer(std::min<uint32>(cdb[4], static_cast<uint32>(InquiryLength)));
	}


	// Fixed format sense (response code 0x70): the key, 10 bytes of additional sense, and the ASC and ASCQ.
	void DecodeRequestSense(std::span<const uint8> cdb) {
		std::fill_n(data.begin(), RequestSenseLength, static_cast<uint8>(0));
		data[0] = 0x70;
		data[2] = sense.key;
		data[7] = 10;
		data[12] = sense.asc;
		data[13] = sense.ascq;
		Answer(std::min<uint32>(cdb[4], static_cast<uint32>(RequestSenseLength)));
	}


	void DecodeReadCapacity() {
		if (!presentCache) {
			Refuse(MscSenseNotPresent);
			return;
		}

		StoreU32Be(data, 0, blockCount - 1);
		StoreU32Be(data, 4, blockSize);
		Answer(static_cast<uint32>(ReadCapacityLength));
	}


	// Current/maximum capacity list: one descriptor of the block count, the code (formatted media, or no media), and the block length.
	void DecodeReadFormatCapacities(std::span<const uint8> cdb) {
		uint32 allocation = LoadU16Be(cdb, 7);
		std::fill_n(data.begin(), ReadFormatCapacitiesLength, static_cast<uint8>(0));
		data[3] = 8;
		StoreU32Be(data, 4, presentCache ? blockCount : 0);
		data[8] = presentCache ? 0x02 : 0x03;
		data[9] = static_cast<uint8>((blockSize >> 16) & 0xFF);
		data[10] = static_cast<uint8>((blockSize >> 8) & 0xFF);
		data[11] = static_cast<uint8>(blockSize & 0xFF);
		Answer(std::min<uint32>(allocation, static_cast<uint32>(ReadFormatCapacitiesLength)));
	}


	// Only the default pages are asked for by hosts. The function has no pages of its own, so 0x3F answers the header alone, and any
	// other page is an invalid field. The device-specific byte carries the write protection.
	void DecodeModeSense6(std::span<const uint8> cdb) {
		if ((cdb[2] & 0x3F) != 0x3F) {
			Refuse(MscSenseInvalidField);
			return;
		}

		std::fill_n(data.begin(), ModeSense6Length, static_cast<uint8>(0));
		data[0] = static_cast<uint8>(ModeSense6Length - 1);
		data[2] = readOnlyCache ? 0x80 : 0x00;
		Answer(std::min<uint32>(cdb[4], static_cast<uint32>(ModeSense6Length)));
	}


	void DecodeModeSense10(std::span<const uint8> cdb) {
		if ((cdb[2] & 0x3F) != 0x3F) {
			Refuse(MscSenseInvalidField);
			return;
		}

		uint32 allocation = LoadU16Be(cdb, 7);
		std::fill_n(data.begin(), ModeSense10Length, static_cast<uint8>(0));
		data[1] = static_cast<uint8>(ModeSense10Length - 2);
		data[3] = readOnlyCache ? 0x80 : 0x00;
		Answer(std::min<uint32>(allocation, static_cast<uint32>(ModeSense10Length)));
	}


	// LOEJ with START clear ejects the medium, unless the host has prevented the removal. Everything else (start, load) passes.
	void DecodeStartStopUnit(std::span<const uint8> cdb) {
		bool start = (cdb[4] & 0x01) != 0;
		bool loadEject = (cdb[4] & 0x02) != 0;
		if (loadEject && !start) {
			if (preventRemoval) {
				Refuse(MscSenseMediumRemovalPrevented);
				return;
			}

			presentCache = false;
			ejectCount++;
			if (ejectDelegate) {
				ejectDelegate();
			}
		}

		Accept();
	}


	// READ(10) and WRITE(10): the block range and the transfer length in blocks. A write to a protected medium fails before any data moves.
	void DecodeMediumTransfer(std::span<const uint8> cdb, bool write) {
		uint32 firstBlock = LoadU32Be(cdb, 2);
		uint32 count = LoadU16Be(cdb, 7);

		if (!presentCache) {
			Refuse(MscSenseNotPresent);
			return;
		}

		if (write && readOnlyCache) {
			Refuse(MscSenseWriteProtected);
			return;
		}

		if (static_cast<uint64>(firstBlock) + count > blockCount) {
			Refuse(MscSenseLbaOutOfRange);
			return;
		}

		uint64 bytes = static_cast<uint64>(count) * blockSize;
		if (bytes > 0xFFFFFFFF) {
			Refuse(MscSenseInvalidField);
			return;
		}

		Accept();
		if (count == 0) {
			return;
		}

		command.deviceDirection = write ? Direction::Out : Direction::In;
		command.deviceLength = static_cast<uint32>(bytes);
		command.medium = write ? Medium::Write : Medium::Read;
		command.firstBlock = firstBlock;
	}


	// VERIFY(10) without the byte-compare: the blocks are read so that a bad block shows. BYTCHK compares with the host's data and is
	// not supported.
	void DecodeVerify(std::span<const uint8> cdb) {
		if ((cdb[1] & 0x02) != 0) {
			Refuse(MscSenseInvalidField);
			return;
		}

		uint32 firstBlock = LoadU32Be(cdb, 2);
		uint32 count = LoadU16Be(cdb, 7);
		if (!presentCache) {
			Refuse(MscSenseNotPresent);
			return;
		}

		if (static_cast<uint64>(firstBlock) + count > blockCount) {
			Refuse(MscSenseLbaOutOfRange);
			return;
		}

		Accept();
		if (count == 0) {
			return;
		}

		command.medium = Medium::Verify;
		command.firstBlock = firstBlock;
		command.blocks = count;
	}


	// The medium work of the command, planned in a critical section: the block of the chunk and the bytes it moves.
	void PlanMedium(Medium work, uint32& firstIndex, uint32& byteCount) {
		if (work == Medium::Read) {
			uint32 remaining = command.expected - command.transferred;
			uint32 chunk = std::min<uint32>(BufferBytes, remaining);
			command.chunkBytes = chunk;
			firstIndex = command.firstBlock + command.transferred / blockSize;
			byteCount = BlocksFor(chunk) * blockSize;
			return;
		}

		if (work == Medium::Write) {
			firstIndex = command.firstBlock + command.chunkStart / blockSize;
			byteCount = command.chunkBytes;
			return;
		}

		if (work == Medium::Verify) {
			uint32 blocksThisChunk = std::min<uint32>(BufferBytes / blockSize, command.blocks);
			command.chunkBytes = blocksThisChunk * blockSize;
			firstIndex = command.firstBlock;
			byteCount = command.chunkBytes;
		}
	}


	// The medium access itself. Runs outside every critical section.
	ResultStatus MediumAccess(Medium work, uint32 firstIndex, uint32 byteCount) {
		if (work == Medium::Read || work == Medium::Verify) {
			return blockDevice.Read(firstIndex, std::span<uint8>(data.data(), byteCount));
		}

		if (work == Medium::Write) {
			return blockDevice.Write(firstIndex, std::span<const uint8>(data.data(), byteCount));
		}

		if (work == Medium::Sync) {
			return blockDevice.Sync();
		}

		return ResultStatus::notSupported;
	}


	// Takes the result of a medium access, in a critical section, and moves the command on. Runs only when no reset happened meanwhile.
	void CommitMedium(USBDeviceContext& context, Medium work, ResultStatus status) {
		if (work == Medium::Read) {
			if (status != ResultStatus::ok) {
				sense = MscSenseUnrecoveredRead;
				command.status = MscStatusFailed;
				command.medium = Medium::None;
				StallIn(context);
				Finish(context);
				return;
			}

			if (ArmIn(context, command.chunkBytes) != ResultStatus::ok) {
				mediumPending = true;
			}

			return;
		}

		if (work == Medium::Write) {
			if (status != ResultStatus::ok) {
				// The chunk was not written, so it does not count toward the residue.
				command.transferred = command.chunkStart;
				sense = MscSenseWriteError;
				command.status = MscStatusFailed;
				command.medium = Medium::None;
				StallOut(context);
				Finish(context);
				return;
			}

			if (command.transferred < command.expected) {
				if (ArmDataOut(context) != ResultStatus::ok) {
					command.status = MscStatusPhaseError;
					command.medium = Medium::None;
					StallOut(context);
					Finish(context);
				}

				return;
			}

			command.medium = Medium::None;
			Finish(context);
			return;
		}

		if (work == Medium::Verify) {
			if (status != ResultStatus::ok) {
				sense = MscSenseUnrecoveredRead;
				command.status = MscStatusFailed;
				command.medium = Medium::None;
				Finish(context);
				return;
			}

			uint32 verified = command.chunkBytes / blockSize;
			command.firstBlock += verified;
			command.blocks -= verified;
			if (command.blocks > 0) {
				mediumPending = true;
				return;
			}

			command.medium = Medium::None;
			Finish(context);
			return;
		}

		if (work == Medium::Sync) {
			if (status != ResultStatus::ok) {
				sense = MscSenseWriteError;
				command.status = MscStatusFailed;
			}

			command.medium = Medium::None;
			Finish(context);
		}
	}


	// Reads the medium's presence and write protection while the function is idle. Only a CBW that arrives in the idle stage reads the
	// cache, and a data stage does not need the medium's state again, so the calls are skipped during a command. IsPresent() may block,
	// so the calls run outside every section. An eject during them makes the sample stale, and the sample is dropped.
	void RefreshMedium() {
		System::CriticalSection(true);
		bool idle = stage == Stage::Idle;
		uint32 ejects = ejectCount;
		uint32 session = generation;
		System::CriticalSection(false);
		if (!idle) {
			return;
		}

		bool present = blockDevice.IsPresent() == ResultStatus::ok;
		bool readOnly = blockDevice.IsReadOnly() == ResultStatus::readOnly;
		System::CriticalSection(true);
		// A sample that an eject or a configuration overtook is dropped. The next Process() reads the medium again.
		if (ejects == ejectCount && session == generation) {
			presentCache = present;
			readOnlyCache = readOnly;
			presenceFresh = true;
		}

		System::CriticalSection(false);
	}


	// The class requests name the function's interface in wIndex, a single byte.
	bool IsOwnInterface(uint16 index) const {
		return index <= 0xFF && OwnsInterface(static_cast<uint8>(index));
	}


	// The command block length an opcode needs: 6 for the group 0 commands and 10 for the group 1 commands (SPC-4 section 4.3). Zero for
	// an opcode the function does not answer, which then fails as an invalid operation code.
	static uint8 RequiredCdbLength(uint8 operation) {
		static constexpr std::array<uint8, 6> SixByte{ ScsiTestUnitReady, ScsiRequestSense, ScsiInquiry, ScsiModeSense6, ScsiStartStopUnit, ScsiPreventAllowMediumRemoval };
		static constexpr std::array<uint8, 7> TenByte{ ScsiReadFormatCapacities, ScsiReadCapacity10, ScsiRead10, ScsiWrite10, ScsiVerify10, ScsiSynchronizeCache10, ScsiModeSense10 };

		if (std::find(SixByte.begin(), SixByte.end(), operation) != SixByte.end()) {
			return 6;
		}

		if (std::find(TenByte.begin(), TenByte.end(), operation) != TenByte.end()) {
			return 10;
		}

		return 0;
	}


	uint32 BlocksFor(uint32 bytes) const {
		return (bytes + blockSize - 1) / blockSize;
	}


	static void CopyPadded(std::span<uint8> target, std::string_view text) {
		for (std::size_t index = 0; index < target.size(); index++) {
			target[index] = index < text.size() ? static_cast<uint8>(text[index]) : static_cast<uint8>(' ');
		}
	}


	static uint32 LoadU32Le(std::span<const uint8> bytes, std::size_t offset) {
		return static_cast<uint32>(bytes[offset]) | (static_cast<uint32>(bytes[offset + 1]) << 8) | (static_cast<uint32>(bytes[offset + 2]) << 16) | (static_cast<uint32>(bytes[offset + 3]) << 24);
	}


	static uint32 LoadU32Be(std::span<const uint8> bytes, std::size_t offset) {
		return (static_cast<uint32>(bytes[offset]) << 24) | (static_cast<uint32>(bytes[offset + 1]) << 16) | (static_cast<uint32>(bytes[offset + 2]) << 8) | static_cast<uint32>(bytes[offset + 3]);
	}


	static uint32 LoadU16Be(std::span<const uint8> bytes, std::size_t offset) {
		return (static_cast<uint32>(bytes[offset]) << 8) | static_cast<uint32>(bytes[offset + 1]);
	}


	static void StoreU32Le(std::span<uint8> bytes, std::size_t offset, uint32 value) {
		for (std::size_t index = 0; index < 4; index++) {
			bytes[offset + index] = static_cast<uint8>((value >> (8 * index)) & 0xFF);
		}
	}


	static void StoreU32Be(std::span<uint8> bytes, std::size_t offset, uint32 value) {
		for (std::size_t index = 0; index < 4; index++) {
			bytes[offset + index] = static_cast<uint8>((value >> (8 * (3 - index))) & 0xFF);
		}
	}
};
