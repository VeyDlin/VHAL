#pragma once
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <array>
#include <cstddef>
#include <span>


// What an audio function reads back from its layout (USBFunctionLayout::descriptors): the stream formats, the endpoints and the
// feature unit controls the builder wrote. The function takes its formats from the descriptors, not from a second copy of the
// application's chain, so the descriptors and the function cannot disagree.
struct USBAudioStreamLayout {
	bool present = false;
	uint8 interfaceNumber = 0;
	uint8 address = 0;
	uint16 packetBytes = 0;
	uint8 channels = 0;
	uint8 rateCount = 0;
	std::array<uint32, USBAudioMaxRates> rates{};
	bool rateControl = false;
	uint8 featureUnit = 0;
	bool volumeControl = false;
	bool muteControl = false;
};


struct USBAudioLayout {
	bool found = false;
	uint8 controlInterface = 0;
	USBAudioStreamLayout microphone;
	USBAudioStreamLayout speaker;
};


// Reads the layout from the descriptor slice of an audio function. The builder wrote the slice, so a well-formed one always parses. A
// malformed one parses as far as it goes: found stays false or a stream stays absent, and the function refuses to configure.
constexpr USBAudioLayout ParseUSBAudioLayout(std::span<const uint8> bytes) {
	USBAudioLayout layout;
	uint8 interfaceNumber = 0;
	uint8 alternate = 0;
	uint8 subClass = 0;
	uint8 microphoneMaster = 0;
	uint8 speakerMaster = 0;
	USBAudioStreamLayout* stream = nullptr;
	std::size_t offset = 0;
	while (offset + 2 <= bytes.size()) {
		std::size_t length = bytes[offset];
		if (length < 2 || offset + length > bytes.size()) {
			break;
		}

		uint8 type = bytes[offset + 1];
		if (type == static_cast<uint8>(USBDescriptorType::Interface)) {
			if (length != 9) {
				break;
			}

			interfaceNumber = bytes[offset + 2];
			alternate = bytes[offset + 3];
			subClass = bytes[offset + 6];
			stream = nullptr;
			if (subClass == USBAudioControlSubClass && !layout.found) {
				layout.found = true;
				layout.controlInterface = interfaceNumber;
			}
		} else if (type == USBAudioClassSpecificInterface && length >= 3) {
			uint8 subtype = bytes[offset + 2];
			if (subClass == USBAudioControlSubClass) {
				// The feature units of the control interface. Their master channel holds the controls.
				if (subtype == USBAudioFeatureUnitSubtype && length >= 7) {
					uint8 unit = bytes[offset + 3];
					if (unit == USBAudioMicrophoneFeature) {
						microphoneMaster = bytes[offset + 6];
					}

					if (unit == USBAudioSpeakerFeature) {
						speakerMaster = bytes[offset + 6];
					}
				}
			} else if (subClass == USBAudioStreamingSubClass && alternate == 1) {
				if (subtype == USBAudioGeneralSubtype && length >= 7) {
					// bTerminalLink is the fourth byte of AS_GENERAL: bLength, type, subtype, then the link, and bDelay after it.
					uint8 link = bytes[offset + 3];
					stream = nullptr;
					if (link == USBAudioMicrophoneOutput) {
						stream = &layout.microphone;
					} else if (link == USBAudioSpeakerInput) {
						stream = &layout.speaker;
					}

					if (stream != nullptr) {
						stream->present = true;
						stream->interfaceNumber = interfaceNumber;
					}
				} else if (subtype == USBAudioFormatTypeSubtype && stream != nullptr && length >= 8) {
					stream->channels = bytes[offset + 4];
					uint8 count = bytes[offset + 7];
					if (count >= 1 && count <= USBAudioMaxRates && length >= 8 + 3 * static_cast<std::size_t>(count)) {
						stream->rateCount = count;
						for (std::size_t index = 0; index < count; index++) {
							std::size_t rateOffset = offset + 8 + 3 * index;
							stream->rates[index] = static_cast<uint32>(bytes[rateOffset])
								| (static_cast<uint32>(bytes[rateOffset + 1]) << 8)
								| (static_cast<uint32>(bytes[rateOffset + 2]) << 16);
						}
					}
				}
			}
		} else if (type == static_cast<uint8>(USBDescriptorType::Endpoint) && stream != nullptr && alternate == 1 && length >= 7) {
			stream->address = bytes[offset + 2];
			stream->packetBytes = static_cast<uint16>(bytes[offset + 4] | (bytes[offset + 5] << 8));
		} else if (type == USBAudioClassSpecificEndpoint && stream != nullptr && alternate == 1 && length >= 4) {
			stream->rateControl = (bytes[offset + 3] & 0x01) != 0;
		}

		offset += length;
	}

	layout.microphone.featureUnit = USBAudioMicrophoneFeature;
	layout.microphone.muteControl = (microphoneMaster & USBAudioMuteBit) != 0;
	layout.microphone.volumeControl = (microphoneMaster & USBAudioVolumeBit) != 0;
	layout.speaker.featureUnit = USBAudioSpeakerFeature;
	layout.speaker.muteControl = (speakerMaster & USBAudioMuteBit) != 0;
	layout.speaker.volumeControl = (speakerMaster & USBAudioVolumeBit) != 0;
	return layout;
}
