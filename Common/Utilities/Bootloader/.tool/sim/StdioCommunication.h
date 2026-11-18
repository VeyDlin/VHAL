#pragma once
#include <Utilities/Bootloader/ICommunication.h>
#include <Drivers/Interface/User/StreamingProtocol/CompactStreamingProtocol.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif


// Simulator transport: CompactStreamingProtocol packets over stdin/stdout. Every packet is framed as
// [u8 length][length bytes] in both directions. A reader thread moves the input packets into a queue, which is
// drained into the protocol from Process() on the main thread.
class StdioCommunication : public ICommunication {
private:
    using Protocol = CompactStreamingProtocol<20, 4>;

    static constexpr size_t PacketSize = 20;

    std::FILE* inputStream;
    std::FILE* outputStream;
    Protocol protocol;
    DataReceivedCallback dataCallback;
    std::mutex queueMutex;
    std::deque<std::vector<uint8>> receivedPackets;
    std::atomic<bool> inputClosed{false};
    std::thread readerThread;


public:
    StdioCommunication() :
        inputStream(stdin),
        outputStream(stdout),
        protocol(
            // Transmit callback: one framed packet to stdout
            [this](std::span<const uint8> data) -> bool {
                return this->WritePacket(data);
            },
            // Data received callback: the protocol has already checked the fragment
            [this](uint8 /*fragmentIndex*/, std::span<const uint8> data) -> bool {
                return this->HandleReceivedData(data);
            },
            // Stream completion: an abandoned or failed outgoing message lost its tail
            [this](Protocol::StreamResult result) -> void {
                if (result != Protocol::StreamResult::Success) {
                    this->NotifyBootloaderFramingLost();
                }
            },
            // Receive reset: a new message started while the previous incoming one was incomplete
            [this]() -> void {
                this->NotifyBootloaderFramingLost();
            }
        )
    {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }


    virtual ~StdioCommunication() = default;


    virtual ResultStatus Initialize() override {
        // The thread is never joined: the simulator leaves the process with std::_Exit()
        readerThread = std::thread([this]() -> void {
            this->ReadLoop();
        });
        readerThread.detach();

        return ResultStatus::ok;
    }


    virtual void Process() override {
        DrainReceivedPackets();
        protocol.Process();
    }


    virtual ResultStatus SendData(std::span<const uint8> data) override {
        Protocol::SendResult result = protocol.Send(data);

        if (result.status == Protocol::SendStatus::Success) {
            return ResultStatus::ok;
        }

        if (result.status == Protocol::SendStatus::Busy) {
            return ResultStatus::busy;
        }

        return ResultStatus::error;
    }


    virtual void SetDataReceivedCallback(DataReceivedCallback callback) override {
        dataCallback = callback;
    }


    virtual bool IsConnected() const override {
        return true;
    }


    virtual bool IsReadyToSend() const override {
        return !protocol.IsTransmitting();
    }


    virtual size_t GetMaxPacketSize() const override {
        return PacketSize;
    }


    // True once stdin reached end of file and every received packet has been handed to the protocol
    bool IsInputFinished() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return inputClosed.load() && receivedPackets.empty();
    }


private:
    void ReadLoop() {
        while (true) {
            uint8 length = 0;

            if (std::fread(&length, 1, 1, inputStream) != 1) {
                break;
            }

            std::vector<uint8> packet(length);

            if (length > 0 && std::fread(packet.data(), 1, length, inputStream) != length) {
                break;
            }

            std::lock_guard<std::mutex> lock(queueMutex);
            receivedPackets.push_back(std::move(packet));
        }

        inputClosed.store(true);
    }


    void DrainReceivedPackets() {
        while (true) {
            std::vector<uint8> packet;

            {
                std::lock_guard<std::mutex> lock(queueMutex);

                if (receivedPackets.empty()) {
                    return;
                }

                packet = std::move(receivedPackets.front());
                receivedPackets.pop_front();
            }

            protocol.DataReceived(packet);
        }
    }


    bool WritePacket(std::span<const uint8> data) {
        if (data.size() > PacketSize) {
            return false;
        }

        uint8 length = static_cast<uint8>(data.size());

        if (std::fwrite(&length, 1, 1, outputStream) != 1) {
            return false;
        }

        if (std::fwrite(data.data(), 1, data.size(), outputStream) != data.size()) {
            return false;
        }

        return std::fflush(outputStream) == 0;
    }


    bool HandleReceivedData(std::span<const uint8> data) {
        // Empty fragments are acknowledged by the protocol and carry no data
        if (!data.empty() && dataCallback) {
            dataCallback(data);
        }

        return true;
    }


    // The empty span means "the transport lost framing": the bootloader drops its partial input and any pending response
    void NotifyBootloaderFramingLost() {
        if (dataCallback) {
            dataCallback(std::span<const uint8>());
        }
    }
};
