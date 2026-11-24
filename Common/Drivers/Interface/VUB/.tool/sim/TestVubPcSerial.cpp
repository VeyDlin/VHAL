#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/PC/SerialLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <thread>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#endif


// The settings, the path of a port, and the serial link. The pure parts are checked on both systems: the settings and the path become
// the DCB on Windows and the termios on Linux. A port is opened only on Linux, on a pseudo-terminal pair (openpty), so no test touches a
// serial port of the machine.


using Serial = VUB::PC::SerialLink;


// Counts the states the link reports.
struct SerialStates {
	int ups = 0;
	int downs = 0;
};


inline void RecordSerialState(void* owner, bool up) {
	SerialStates* states = static_cast<SerialStates*>(owner);
	if (up) {
		states->ups++;
	} else {
		states->downs++;
	}
}


// The bytes a receiver of the link was fed.
struct SerialInbox {
	std::array<uint8, 64> bytes{};
	std::size_t count = 0;
};


inline void FeedSerialInbox(void* owner, std::span<const uint8> bytes) {
	SerialInbox* inbox = static_cast<SerialInbox*>(owner);
	for (std::size_t index = 0; index < bytes.size() && inbox->count < inbox->bytes.size(); index++) {
		inbox->bytes[inbox->count] = bytes[index];
		inbox->count++;
	}
}


inline void FeedSerialPacketNothing(void*, std::span<const uint8>) {
}


inline std::size_t SerialRoomOpen(void*) {
	return 64;
}


inline void CheckSettings() {
	Serial::Settings settings;
	SIM_EXPECT(Serial::ValidSettings(settings));

	Serial::Settings tooFewBits;
	tooFewBits.dataBits = 4;
	SIM_EXPECT(!Serial::ValidSettings(tooFewBits));

	Serial::Settings tooManyBits;
	tooManyBits.dataBits = 9;
	SIM_EXPECT(!Serial::ValidSettings(tooManyBits));

	Serial::Settings noBaud;
	noBaud.baud = 0;
	SIM_EXPECT(!Serial::ValidSettings(noBaud));
}


inline void CheckPaths() {
#if defined(_WIN32)
	SIM_EXPECT(Serial::PathOf("11") == "\\\\.\\COM11");
	SIM_EXPECT(Serial::PathOf("COM3") == "\\\\.\\COM3");
	SIM_EXPECT(Serial::PathOf("\\\\.\\COM10") == "\\\\.\\COM10");
#else
	SIM_EXPECT(Serial::PathOf("ttyUSB0") == "/dev/ttyUSB0");
	SIM_EXPECT(Serial::PathOf("/dev/pts/3") == "/dev/pts/3");
#endif
}


inline void CheckLineSettings() {
#if defined(_WIN32)
	DCB dcb{};
	Serial::Settings odd;
	odd.baud = 57600;
	odd.dataBits = 7;
	odd.parity = Serial::Parity::odd;
	odd.stopBits = Serial::StopBits::two;
	Serial::ToDcb(dcb, odd);
	SIM_EXPECT(dcb.BaudRate == 57600);
	SIM_EXPECT(dcb.ByteSize == 7);
	SIM_EXPECT(dcb.Parity == ODDPARITY);
	SIM_EXPECT(dcb.fParity == TRUE);
	SIM_EXPECT(dcb.StopBits == TWOSTOPBITS);
	SIM_EXPECT(dcb.fBinary == TRUE);
	SIM_EXPECT(dcb.fDtrControl == DTR_CONTROL_ENABLE);

	Serial::Settings none;
	Serial::ToDcb(dcb, none);
	SIM_EXPECT(dcb.Parity == NOPARITY);
	SIM_EXPECT(dcb.fParity == FALSE);
	SIM_EXPECT(dcb.StopBits == ONESTOPBIT);
	SIM_EXPECT(dcb.ByteSize == 8);
#else
	termios plain{};
	SIM_EXPECT(Serial::ToTermios(plain, Serial::Settings()));
	SIM_EXPECT(cfgetispeed(&plain) == B115200);
	SIM_EXPECT(cfgetospeed(&plain) == B115200);
	SIM_EXPECT((plain.c_cflag & CSIZE) == CS8);
	SIM_EXPECT((plain.c_cflag & PARENB) == 0);
	SIM_EXPECT((plain.c_cflag & CSTOPB) == 0);
	SIM_EXPECT((plain.c_cflag & CLOCAL) != 0);
	SIM_EXPECT((plain.c_cflag & CREAD) != 0);
	SIM_EXPECT((plain.c_lflag & ICANON) == 0);
	SIM_EXPECT((plain.c_lflag & ECHO) == 0);

	termios odd{};
	Serial::Settings oddSettings;
	oddSettings.baud = 9600;
	oddSettings.dataBits = 7;
	oddSettings.parity = Serial::Parity::odd;
	oddSettings.stopBits = Serial::StopBits::two;
	SIM_EXPECT(Serial::ToTermios(odd, oddSettings));
	SIM_EXPECT(cfgetospeed(&odd) == B9600);
	SIM_EXPECT((odd.c_cflag & CSIZE) == CS7);
	SIM_EXPECT((odd.c_cflag & PARENB) != 0);
	SIM_EXPECT((odd.c_cflag & PARODD) != 0);
	SIM_EXPECT((odd.c_cflag & CSTOPB) != 0);

	termios refused{};
	Serial::Settings oddBaud;
	oddBaud.baud = 1234;
	SIM_EXPECT(!Serial::ToTermios(refused, oddBaud));

	// Flow control that another program left on the port is cleared, as on Windows: hardware (CRTSCTS) and software (IXON, IXOFF).
	termios flowed{};
	flowed.c_cflag |= CRTSCTS;
	flowed.c_iflag |= IXON | IXOFF;
	SIM_EXPECT(Serial::ToTermios(flowed, Serial::Settings()));
	SIM_EXPECT((flowed.c_cflag & CRTSCTS) == 0);
	SIM_EXPECT((flowed.c_iflag & (IXON | IXOFF)) == 0);
	SIM_EXPECT((flowed.c_cflag & (CLOCAL | CREAD)) == (CLOCAL | CREAD));
#endif
}


#if defined(__linux__)
// Opens the link on a pseudo-terminal pair: the link writes to the slave, the test reads the master; the test writes the master and the
// link polls the slave. The settings are read from the slave, which shares its termios with the master.
inline void CheckPseudoTerminal() {
	int master = -1;
	int slave = -1;
	char name[256] = {};
	SIM_EXPECT(openpty(&master, &slave, name, nullptr, nullptr) == 0);

	SerialStates states;
	SerialInbox inbox;
	Serial link;
	link.SetStateSink(VUB::StateSink(&states, &RecordSerialState));
	VUB::Receiver receiver(&inbox, &FeedSerialInbox, &FeedSerialPacketNothing, &SerialRoomOpen);

	Serial::Settings settings;
	settings.baud = 9600;
	settings.dataBits = 7;
	settings.parity = Serial::Parity::odd;
	settings.stopBits = Serial::StopBits::two;
	SIM_EXPECT(link.Open(name, settings) == ResultStatus::ok);
	SIM_EXPECT(link.IsOpen());
	SIM_EXPECT(states.ups == 1);

	// The pseudo-terminal driver keeps eight data bits and no parity whatever the termios says, so the data bits and the parity are
	// checked by the pure part above. The baud rate and the stop bits reach the pair.
	termios term{};
	SIM_EXPECT(tcgetattr(slave, &term) == 0);
	SIM_EXPECT(cfgetospeed(&term) == B9600);
	SIM_EXPECT((term.c_cflag & CSTOPB) != 0);

	fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
	const std::array<uint8, 3> out = {1, 2, 3};
	SIM_EXPECT(link.Write(out).Value() == 3);
	std::array<uint8, 8> echoed{};
	std::size_t read = 0;
	for (int step = 0; step < 200 && read < 3; step++) {
		ssize_t got = ::read(master, echoed.data() + read, echoed.size() - read);
		if (got > 0) {
			read += static_cast<std::size_t>(got);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(read == 3);
	SIM_EXPECT(echoed[0] == 1 && echoed[2] == 3);

	const std::array<uint8, 2> incoming = {4, 5};
	SIM_EXPECT(::write(master, incoming.data(), incoming.size()) == 2);
	for (int step = 0; step < 200 && inbox.count < 2; step++) {
		link.Poll(receiver);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(inbox.count == 2);
	SIM_EXPECT(inbox.bytes[0] == 4 && inbox.bytes[1] == 5);

	// The master goes away, as an unplugged adapter would. The link reports down, closes, and can open again.
	::close(master);
	for (int step = 0; step < 200 && link.IsOpen(); step++) {
		link.Poll(receiver);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(!link.IsOpen());
	SIM_EXPECT(states.downs == 1);

	::close(slave);
	int newMaster = -1;
	int newSlave = -1;
	char newName[256] = {};
	SIM_EXPECT(openpty(&newMaster, &newSlave, newName, nullptr, nullptr) == 0);
	SIM_EXPECT(link.Open(newName, settings) == ResultStatus::ok);
	SIM_EXPECT(link.IsOpen());
	SIM_EXPECT(states.ups == 2);
	link.Close();
	SIM_EXPECT(states.downs == 2);
	::close(newMaster);
	::close(newSlave);
}
#endif


#if defined(__linux__)
// The payloads the two stacks of the pair receive.
inline std::size_t pairReceived = 0;
inline std::array<uint8, 16> pairLast{};
inline std::size_t pairLastLength = 0;


inline void PairReceive(const VUB::Meta&, std::span<const uint8> payload) {
	pairReceived++;
	pairLastLength = payload.size() < pairLast.size() ? payload.size() : pairLast.size();
	for (std::size_t index = 0; index < pairLastLength; index++) {
		pairLast[index] = payload[index];
	}
}


inline uint32 PairClockUs() {
	return 0;
}


// COBS framing over a serial link.
using PairStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<Serial>()
		.MaxPayload(16)
		.ReceiveRing(256)
		.OnReceive<&PairReceive>()
		.Clock<&PairClockUs>();
}>;


// Copies what is waiting on one master to the other master: the test is the wire between the two pseudo-terminals.
inline void Forward(int from, int to) {
	std::array<uint8, 256> buffer{};
	ssize_t got = ::read(from, buffer.data(), buffer.size());
	if (got > 0) {
		ssize_t wrote = ::write(to, buffer.data(), static_cast<std::size_t>(got));
		SIM_EXPECT(wrote == got);
	}
}


// Two stacks, each on a serial link opened on its own pseudo-terminal. A packet that one stack sends arrives at the other, through the
// two pairs and the wire the test forwards between the masters.
inline void CheckStacksOverPairs() {
	int masterA = -1;
	int slaveA = -1;
	char nameA[256] = {};
	int masterB = -1;
	int slaveB = -1;
	char nameB[256] = {};
	SIM_EXPECT(openpty(&masterA, &slaveA, nameA, nullptr, nullptr) == 0);
	SIM_EXPECT(openpty(&masterB, &slaveB, nameB, nullptr, nullptr) == 0);
	fcntl(masterA, F_SETFL, fcntl(masterA, F_GETFL) | O_NONBLOCK);
	fcntl(masterB, F_SETFL, fcntl(masterB, F_GETFL) | O_NONBLOCK);

	Serial linkA;
	Serial linkB;
	PairStack stackA(linkA);
	PairStack stackB(linkB);
	Serial::Settings settings;
	SIM_EXPECT(linkA.Open(nameA, settings) == ResultStatus::ok);
	SIM_EXPECT(linkB.Open(nameB, settings) == ResultStatus::ok);

	pairReceived = 0;
	const std::array<uint8, 3> payload = {1, 2, 3};
	SIM_EXPECT(stackA.Send(payload) == ResultStatus::ok);
	for (int step = 0; step < 1000 && pairReceived < 1; step++) {
		stackA.Process();
		stackB.Process();
		Forward(masterA, masterB);
		Forward(masterB, masterA);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(pairReceived == 1);
	SIM_EXPECT(pairLastLength == 3);
	SIM_EXPECT(pairLast[0] == 1 && pairLast[2] == 3);

	const std::array<uint8, 2> answer = {9, 8};
	SIM_EXPECT(stackB.Send(answer) == ResultStatus::ok);
	for (int step = 0; step < 1000 && pairReceived < 2; step++) {
		stackA.Process();
		stackB.Process();
		Forward(masterA, masterB);
		Forward(masterB, masterA);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	SIM_EXPECT(pairReceived == 2);
	SIM_EXPECT(pairLastLength == 2 && pairLast[0] == 9);

	linkA.Close();
	linkB.Close();
	::close(masterA);
	::close(slaveA);
	::close(masterB);
	::close(slaveB);
}


// A link opened before its stack is built: the stack gives its sink later, and learns at once that the link is up, so it sends.
inline void CheckStackBuiltAfterOpen() {
	int master = -1;
	int slave = -1;
	char name[256] = {};
	SIM_EXPECT(openpty(&master, &slave, name, nullptr, nullptr) == 0);
	fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);

	Serial link;
	Serial::Settings settings;
	SIM_EXPECT(link.Open(name, settings) == ResultStatus::ok);
	PairStack stack(link);
	stack.Process();
	SIM_EXPECT(stack.LinkUp());

	const std::array<uint8, 3> payload = {4, 5, 6};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	stack.Process();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	std::array<uint8, 64> wire{};
	ssize_t got = ::read(master, wire.data(), wire.size());
	SIM_EXPECT(got > 0);

	link.Close();
	::close(master);
	::close(slave);
}
#endif


int main() {
	CheckSettings();
	CheckPaths();
	CheckLineSettings();
#if defined(__linux__)
	CheckPseudoTerminal();
	CheckStacksOverPairs();
	CheckStackBuiltAfterOpen();
#endif
	return SimFinish("TestVubPcSerial");
}
