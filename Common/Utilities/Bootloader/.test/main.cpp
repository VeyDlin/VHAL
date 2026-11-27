#include <VHAL.h>
#include <Utilities/Bootloader/Loader/AESFLASHBootloader.h>
#include "StdioCommunication.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>


// Erase step of the simulated flash (one call of OnEraseIteration)
static constexpr uint32 kEraseStep = 0x1000;

// Flash image written with --flash-out and the storage behind the UserData command
static SimFlash* gFlash = nullptr;
static std::string gFlashOutPath;
static std::map<uint8, std::vector<uint8>> gUserData;


struct SimOptions {
    bool aesEnabled = false;
    uint32 keyBits = 0;
    AESMode aesMode = AESMode::CTR;
    std::vector<uint8> key;
    std::array<uint8, 16> iv{};
    uint32 regionStart = 0x08020000;
    uint32 regionSize = 0x40000;
    uint32 alignment = 4;
    std::string flashIn;
    std::string flashOut;
    uint32 rdpLevel = 0;
    bool allowUpdates = false;
    bool strictProgram = false;
    bool userData = false;
};


[[noreturn]] static void Fail(const std::string& message) {
    std::fprintf(stderr, "vboot-sim: error: %s\n", message.c_str());
    std::exit(2);
}


static std::string NextValue(int argc, char** argv, int& index) {
    // A following option is not a value; a value that really starts with "--" can be given as "./--name"
    if (index + 1 >= argc || std::string(argv[index + 1]).rfind("--", 0) == 0) {
        Fail(std::string("missing value for ") + argv[index]);
    }

    index++;
    return std::string(argv[index]);
}


static uint32 ParseU32(const std::string& text, const std::string& name) {
    if (text.empty() || text[0] == '-' || text[0] == '+') {
        Fail("invalid number for " + name + ": '" + text + "'");
    }

    // Base 0 would read a leading zero as octal, so "0100" would differ from the host, which reads it as decimal
    const bool isHex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    char* end = nullptr;
    unsigned long long value = std::strtoull(text.c_str(), &end, isHex ? 16 : 10);

    if (*end != '\0' || value > 0xFFFFFFFFULL) {
        Fail("invalid number for " + name + ": '" + text + "'");
    }

    return static_cast<uint32>(value);
}


static uint8 HexNibble(char digit, const std::string& name) {
    if (digit >= '0' && digit <= '9') {
        return static_cast<uint8>(digit - '0');
    }

    if (digit >= 'a' && digit <= 'f') {
        return static_cast<uint8>(10 + digit - 'a');
    }

    if (digit >= 'A' && digit <= 'F') {
        return static_cast<uint8>(10 + digit - 'A');
    }

    Fail(name + " contains a character that is not a hex digit");
}


static std::vector<uint8> ParseHex(const std::string& text, const std::string& name) {
    if (text.size() % 2 != 0) {
        Fail(name + " must have an even number of hex digits");
    }

    std::vector<uint8> bytes;

    for (size_t index = 0; index < text.size(); index += 2) {
        uint8 high = HexNibble(text[index], name);
        uint8 low = HexNibble(text[index + 1], name);
        bytes.push_back(static_cast<uint8>((high << 4) | low));
    }

    return bytes;
}


static void ParseAesCipher(const std::string& cipherName, SimOptions& options) {
    const std::string usage = "cipher must be none or aes-<128|192|256>-<ecb|cbc|ctr>, got '" + cipherName + "'";

    if (cipherName.rfind("aes-", 0) != 0) {
        Fail(usage);
    }

    std::string rest = cipherName.substr(4);
    size_t dash = rest.find('-');

    if (dash == std::string::npos) {
        Fail(usage);
    }

    std::string bits = rest.substr(0, dash);
    std::string mode = rest.substr(dash + 1);

    if (bits == "128") {
        options.keyBits = 128;
    } else if (bits == "192") {
        options.keyBits = 192;
    } else if (bits == "256") {
        options.keyBits = 256;
    } else {
        Fail(usage);
    }

    if (mode == "ecb") {
        options.aesMode = AESMode::ECB;
    } else if (mode == "cbc") {
        options.aesMode = AESMode::CBC;
    } else if (mode == "ctr") {
        options.aesMode = AESMode::CTR;
    } else {
        Fail(usage);
    }

    options.aesEnabled = true;
}


static SimOptions ParseOptions(int argc, char** argv) {
    SimOptions options;
    std::string cipherName = "none";
    std::string keyText;
    bool keyGiven = false;
    std::string ivText;
    bool ivGiven = false;

    for (int index = 1; index < argc; index++) {
        std::string arg = argv[index];

        if (arg == "--cipher") {
            cipherName = NextValue(argc, argv, index);
        } else if (arg == "--key") {
            keyText = NextValue(argc, argv, index);
            keyGiven = true;
        } else if (arg == "--iv") {
            ivText = NextValue(argc, argv, index);
            ivGiven = true;
        } else if (arg == "--region-start") {
            options.regionStart = ParseU32(NextValue(argc, argv, index), arg);
        } else if (arg == "--region-size") {
            options.regionSize = ParseU32(NextValue(argc, argv, index), arg);
        } else if (arg == "--alignment") {
            options.alignment = ParseU32(NextValue(argc, argv, index), arg);
        } else if (arg == "--flash-in") {
            options.flashIn = NextValue(argc, argv, index);
        } else if (arg == "--flash-out") {
            options.flashOut = NextValue(argc, argv, index);
        } else if (arg == "--rdp") {
            options.rdpLevel = ParseU32(NextValue(argc, argv, index), arg);
        } else if (arg == "--allow-updates-when-read-protected") {
            options.allowUpdates = true;
        } else if (arg == "--strict-program") {
            options.strictProgram = true;
        } else if (arg == "--user-data") {
            options.userData = true;
        } else {
            Fail("unknown argument: " + arg);
        }
    }

    if (cipherName != "none") {
        ParseAesCipher(cipherName, options);
    }

    if (options.aesEnabled) {
        if (!keyGiven) {
            Fail("--key is required with --cipher " + cipherName);
        }

        options.key = ParseHex(keyText, "--key");

        if (options.key.size() * 8 != options.keyBits) {
            Fail("--key must have " + std::to_string(options.keyBits / 8) + " bytes for --cipher " + cipherName);
        }
    } else if (keyGiven) {
        Fail("--key needs an AES --cipher");
    }

    if (ivGiven) {
        std::vector<uint8> iv = ParseHex(ivText, "--iv");

        if (iv.size() != options.iv.size()) {
            Fail("--iv must be 16 bytes (32 hex digits)");
        }

        std::copy(iv.begin(), iv.end(), options.iv.begin());
    }

    bool alignmentIsPowerOfTwo = options.alignment != 0 && (options.alignment & (options.alignment - 1)) == 0;

    if (!alignmentIsPowerOfTwo || options.alignment > 8) {
        Fail("--alignment must be 1, 2, 4 or 8");
    }

    if (options.aesEnabled && options.alignment != 4) {
        Fail("the AES bootloaders use write alignment 4");
    }

    if (options.regionSize <= 8) {
        Fail("--region-size must be larger than the 8-byte firmware header");
    }

    if (options.regionStart % options.alignment != 0 || options.regionSize % options.alignment != 0) {
        Fail("--region-start and --region-size must be multiples of --alignment");
    }

    if (static_cast<uint64>(options.regionStart) + options.regionSize > 0x100000000ULL) {
        Fail("the flash region exceeds the 32-bit address space");
    }

    if (options.rdpLevel > 2) {
        Fail("--rdp must be 0, 1 or 2");
    }

    return options;
}


static void LoadFlashIn(SimFlash& flash, const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        Fail("cannot open --flash-in " + path);
    }

    std::vector<uint8> content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (content.size() > flash.memory.size()) {
        Fail("--flash-in is larger than the flash region");
    }

    std::copy(content.begin(), content.end(), flash.memory.begin());
}


// Returns false when the requested --flash-out image could not be written completely
static bool WriteFlashOut() {
    if (gFlashOutPath.empty() || gFlash == nullptr) {
        return true;
    }

    std::FILE* file = std::fopen(gFlashOutPath.c_str(), "wb");

    if (file == nullptr) {
        std::fprintf(stderr, "vboot-sim: warning: cannot write --flash-out %s\n", gFlashOutPath.c_str());
        return false;
    }

    size_t written = std::fwrite(gFlash->memory.data(), 1, gFlash->memory.size(), file);
    std::fclose(file);

    if (written != gFlash->memory.size()) {
        std::fprintf(stderr, "vboot-sim: warning: short write to --flash-out %s\n", gFlashOutPath.c_str());
        return false;
    }

    return true;
}


// Writes the flash image and leaves the process with status 0, or 1 when the --flash-out write failed; stdio is flushed, no destructors run
[[noreturn]] static void ExitSimulator(const char* event) {
    bool flashWritten = WriteFlashOut();

    if (event != nullptr) {
        std::fprintf(stderr, "%s\n", event);
    }

    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(flashWritten ? 0 : 1);
}


// The bootloader under test: the start and erase hooks are simulator specific
template<typename Base>
class SimBootloader : public Base {
public:
    template<typename... Args>
    explicit SimBootloader(Args&&... args) : Base(std::forward<Args>(args)...) { }


protected:
    bool OnCheckBootloaderRequest() const override {
        return true;
    }


    bool OnStartApplication() override {
        ExitSimulator("START");
    }


    ResultStatus OnEraseIteration(uint32 address, uint32 endAddress, uint32 /*iteration*/, uint32& bytesErased) override {
        uint32 chunk = std::min(kEraseStep, endAddress - address);
        ResultStatus status = this->flashAdapter.Erase(address, chunk);

        if (status != ResultStatus::ok) {
            return status;
        }

        bytesErased = chunk;

        if (address + chunk >= endAddress) {
            return ResultStatus::ok;
        }

        return ResultStatus::inProgress;
    }
};


template<typename Bootloader>
static void ConfigureBootloader(Bootloader& bootloader, const SimOptions& options) {
    bootloader.allowUpdatesWhenReadProtected = options.allowUpdates;

    if (!options.userData) {
        return;
    }

    bootloader.SetUserDataHandler(
        [](uint8 operation, uint8 dataType, std::span<const uint8> data) -> std::span<const uint8> {
            if (operation == 1) {
                gUserData[dataType].assign(data.begin(), data.end());
                return {};
            }

            std::map<uint8, std::vector<uint8>>::const_iterator found = gUserData.find(dataType);

            if (found == gUserData.end()) {
                return {};
            }

            return std::span<const uint8>(found->second.data(), found->second.size());
        }
    );
}


template<typename Bootloader>
static int RunBootloader(Bootloader& bootloader, StdioCommunication& comm) {
    if (bootloader.Initialize() != ResultStatus::ok) {
        std::fprintf(stderr, "vboot-sim: error: the bootloader did not start (RDP level 2 or invalid configuration)\n");
        return 1;
    }

    while (true) {
        bootloader.Process();

        if (System::resetRequested) {
            ExitSimulator("RESET");
        }

        if (comm.IsInputFinished()) {
            ExitSimulator(nullptr);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


static int RunPlainBootloader(StdioCommunication& comm, SimFlash& flash, const SimOptions& options) {
    using Device = SimBootloader<FLASHBootloader<>>;

    Device bootloader(comm, flash, options.regionStart, options.regionSize, options.alignment);
    ConfigureBootloader(bootloader, options);
    return RunBootloader(bootloader, comm);
}


template<AESKeySize KeySize>
static int RunAesBootloader(StdioCommunication& comm, SimFlash& flash, const SimOptions& options) {
    using Device = SimBootloader<AESFLASHBootloader<KeySize>>;

    Device bootloader(comm, flash, options.aesMode, options.key, options.regionStart, options.regionSize, options.iv);
    ConfigureBootloader(bootloader, options);
    return RunBootloader(bootloader, comm);
}


int main(int argc, char** argv) {
    SimOptions options = ParseOptions(argc, argv);

    SimFlash flash(options.regionStart, options.regionSize);
    flash.requiredAlignment = options.alignment;
    flash.requireErasedForProgram = options.strictProgram;
    flash.readProtected = options.rdpLevel >= 1;

    if (options.rdpLevel == 0) {
        flash.level = SimFlash::FlashProtectionLevel::Level0;
    } else if (options.rdpLevel == 1) {
        flash.level = SimFlash::FlashProtectionLevel::Level1;
    } else {
        flash.level = SimFlash::FlashProtectionLevel::Level2;
    }

    if (!options.flashIn.empty()) {
        LoadFlashIn(flash, options.flashIn);
    }

    gFlash = &flash;
    gFlashOutPath = options.flashOut;

    StdioCommunication comm;

    if (!options.aesEnabled) {
        return RunPlainBootloader(comm, flash, options);
    }

    if (options.keyBits == 128) {
        return RunAesBootloader<AESKeySize::AES128>(comm, flash, options);
    }

    if (options.keyBits == 192) {
        return RunAesBootloader<AESKeySize::AES192>(comm, flash, options);
    }

    return RunAesBootloader<AESKeySize::AES256>(comm, flash, options);
}
