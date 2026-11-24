#pragma once
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/PC/TransferRelease.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#else
#error "VUB::PC::SerialLink is written for Windows and Linux only"
#endif

namespace VUB::PC {


// A serial port (a UART behind a USB-serial adapter, or a CDC device) as a byte stream. Open sets the line settings, the port is
// raw and non-blocking, and Poll reads what is waiting, up to the receiver's room. Write takes what the port accepts at once and
// returns zero while the port is full. On Windows the port is opened for overlapped I/O with the link's own buffers and events; on
// Linux it is opened with O_NONBLOCK. DTR is set on open, so a CDC device sees the terminal open.
//
// An I/O error (an unplugged adapter) reports the link down and closes the port without waiting: on Windows the transfers in flight
// are cancelled, and Poll frees the handles once they have completed. Open again to come back. The link is not reliable.
//
// Close waits up to 200 ms for the transfers in flight; one still in flight after that is released by Poll. The destructor waits until
// every transfer has completed. Open returns busy while an earlier release is still waiting for its transfers.
class SerialLink {
public:
	enum class Parity : std::uint8_t {
		none,
		odd,
		even
	};


	enum class StopBits : std::uint8_t {
		one,
		two
	};


	// The line settings. dataBits is 5 to 8 and baud is above zero; other values are refused by Open.
	struct Settings {
		std::uint32_t baud = 115200;
		std::uint8_t dataBits = 8;
		Parity parity = Parity::none;
		StopBits stopBits = StopBits::one;
	};


	static constexpr LinkTraits traits = LinkTraits().FullDuplex().ReportsState().MaxPacket(256);


private:
	// The handle layer of a release of this link (see TransferRelease).
	class ReleaseLayer {
	public:
		explicit ReleaseLayer(SerialLink& linkObject) : link(linkObject) {
		}


		void CancelAll() {
			link.CancelTransfers();
		}


		bool InFlight() const {
			return link.TransfersInFlight();
		}


		void Progress(uint32 milliseconds) {
			link.ProgressTransfers(milliseconds);
		}


		void Free() {
			link.FreeHandles();
		}


	private:
		SerialLink& link;
	};


	StateSink sink;
	bool portOpen = false;
	std::array<std::uint8_t, 64> readBuffer{};
	std::size_t readOffset = 0;
	std::size_t readAvailable = 0;
	TransferRelease<ReleaseLayer> release;
#if defined(_WIN32)
	HANDLE handle = INVALID_HANDLE_VALUE;
	HANDLE readEvent = nullptr;
	HANDLE writeEvent = nullptr;
	OVERLAPPED readOverlapped{};
	OVERLAPPED writeOverlapped{};
	bool readPending = false;
	bool writePending = false;
	std::array<std::uint8_t, 64> writeBuffer{};
#else
	int descriptor = -1;
#endif


public:
	SerialLink() = default;


	SerialLink(const SerialLink&) = delete;
	SerialLink& operator=(const SerialLink&) = delete;


	~SerialLink() {
		Shut(ReleaseWait::Unbounded);
	}


	// The path of a port. On Windows "11", "COM11" and "\\.\COM11" name the same port; a number is COM plus the number. On Linux a
	// name without a slash is taken under /dev, so "ttyUSB0" is "/dev/ttyUSB0".
	static std::string PathOf(const std::string& port) {
#if defined(_WIN32)
		if (port.rfind("\\\\.\\", 0) == 0) {
			return port;
		}

		bool digits = !port.empty();
		for (char symbol : port) {
			if (symbol < '0' || symbol > '9') {
				digits = false;
			}
		}

		if (digits) {
			return "\\\\.\\COM" + port;
		}

		return "\\\\.\\" + port;
#else
		if (!port.empty() && port[0] == '/') {
			return port;
		}

		return "/dev/" + port;
#endif
	}


	// Whether the settings can be given to a port: five to eight data bits and a positive baud rate.
	static bool ValidSettings(const Settings& settings) {
		return settings.baud > 0 && settings.dataBits >= 5 && settings.dataBits <= 8;
	}


#if defined(_WIN32)
	// Writes the settings into a DCB. The DCB is a copy of the port's current state, so the flow control and the other fields that the
	// settings do not name are kept.
	static void ToDcb(DCB& dcb, const Settings& settings) {
		dcb.BaudRate = settings.baud;
		dcb.ByteSize = settings.dataBits;
		dcb.Parity = NOPARITY;
		if (settings.parity == Parity::odd) {
			dcb.Parity = ODDPARITY;
		}

		if (settings.parity == Parity::even) {
			dcb.Parity = EVENPARITY;
		}

		dcb.fParity = settings.parity != Parity::none ? TRUE : FALSE;
		dcb.StopBits = settings.stopBits == StopBits::two ? TWOSTOPBITS : ONESTOPBIT;
		dcb.fBinary = TRUE;
		dcb.fOutxCtsFlow = FALSE;
		dcb.fOutxDsrFlow = FALSE;
		dcb.fDtrControl = DTR_CONTROL_ENABLE;
		dcb.fDsrSensitivity = FALSE;
		dcb.fOutX = FALSE;
		dcb.fInX = FALSE;
		dcb.fErrorChar = FALSE;
		dcb.fNull = FALSE;
		dcb.fRtsControl = RTS_CONTROL_ENABLE;
		dcb.fAbortOnError = FALSE;
	}
#else
	// Writes the settings into a termios: raw mode, then the speed, the data bits, the parity and the stop bits. Returns false when the
	// baud rate is not one that termios has, or the settings are not valid.
	static bool ToTermios(termios& term, const Settings& settings) {
		speed_t speed = B0;
		if (!ValidSettings(settings) || !SpeedOf(settings.baud, speed)) {
			return false;
		}

		cfmakeraw(&term);
		cfsetispeed(&term, speed);
		cfsetospeed(&term, speed);
		term.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB | PARODD | CSTOPB);
		term.c_cflag |= CLOCAL | CREAD;
		if (settings.dataBits == 5) {
			term.c_cflag |= CS5;
		}

		if (settings.dataBits == 6) {
			term.c_cflag |= CS6;
		}

		if (settings.dataBits == 7) {
			term.c_cflag |= CS7;
		}

		if (settings.dataBits == 8) {
			term.c_cflag |= CS8;
		}

		if (settings.parity != Parity::none) {
			term.c_cflag |= PARENB;
		}

		if (settings.parity == Parity::odd) {
			term.c_cflag |= PARODD;
		}

		if (settings.stopBits == StopBits::two) {
			term.c_cflag |= CSTOPB;
		}

		return true;
	}
#endif


	// Opens the port with the settings. Returns ok when the link is open and up; an error otherwise, and the link stays closed. Opening a
	// link that is open closes it first. Returns busy while an earlier release still waits for its transfers.
	ResultStatus Open(const std::string& port, const Settings& settings) {
		Close();
		if (release.Closing()) {
			return ResultStatus::busy;
		}

		if (!ValidSettings(settings)) {
			return ResultStatus::invalidParameter;
		}

		std::string path = PathOf(port);
#if defined(_WIN32)
		HANDLE opened = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (opened == INVALID_HANDLE_VALUE) {
			return ErrorOf(GetLastError());
		}

		DCB dcb{};
		dcb.DCBlength = sizeof(DCB);
		if (!GetCommState(opened, &dcb)) {
			CloseHandle(opened);
			return ResultStatus::ioError;
		}

		ToDcb(dcb, settings);
		if (!SetCommState(opened, &dcb)) {
			CloseHandle(opened);
			return ResultStatus::invalidParameter;
		}

		COMMTIMEOUTS timeouts{};
		timeouts.ReadIntervalTimeout = MAXDWORD;
		if (!SetCommTimeouts(opened, &timeouts)) {
			CloseHandle(opened);
			return ResultStatus::ioError;
		}

		EscapeCommFunction(opened, SETDTR);
		PurgeComm(opened, PURGE_RXCLEAR | PURGE_TXCLEAR);
		handle = opened;
		readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		readOverlapped.hEvent = readEvent;
		writeOverlapped.hEvent = writeEvent;
#else
		int opened = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
		if (opened < 0) {
			return ErrorOf(errno);
		}

		termios term{};
		if (tcgetattr(opened, &term) != 0 || !ToTermios(term, settings)) {
			::close(opened);
			return ResultStatus::invalidParameter;
		}

		if (tcsetattr(opened, TCSANOW, &term) != 0) {
			::close(opened);
			return ResultStatus::ioError;
		}

		int dtr = TIOCM_DTR;
		ioctl(opened, TIOCMBIS, &dtr);
		tcflush(opened, TCIOFLUSH);
		descriptor = opened;
#endif
		portOpen = true;
		sink.Report(true);
		return ResultStatus::ok;
	}


	// Closes the port. Waits up to 200 ms for the transfers in flight; one still in flight after that is released by Poll. Reports the link
	// down when it was up.
	void Close() {
		bool wasOpen = portOpen;
		Shut(ReleaseWait::Bounded);
		if (wasOpen) {
			sink.Report(false);
		}
	}


	bool IsOpen() const {
		return portOpen;
	}


	// The stack gives its sink when it is built, which may be after Open. A stack starts with its link down, so a link that is up
	// reports it into the sink at once.
	void SetStateSink(StateSink value) {
		sink = value;
		if (IsOpen()) {
			sink.Report(true);
		}
	}


	// Reads what is waiting, up to the receiver's room, and feeds it. Never waits.
	void Poll(Receiver receiver) {
		ReleaseLayer layer(*this);
		release.Finish(layer, ReleaseWait::None);
		if (!portOpen) {
			return;
		}

		std::size_t room = receiver.Room();
		if (readAvailable == 0 && room > 0) {
			ReadStep();
		}

		if (!portOpen || readAvailable == 0) {
			return;
		}

		std::size_t take = readAvailable < room ? readAvailable : room;
		receiver.Feed(std::span<const uint8>(readBuffer.data() + readOffset, take));
		readOffset += take;
		readAvailable -= take;
	}


	// Takes the bytes the port accepts at once. Returns zero while the port is busy, or when it is closed.
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		if (!portOpen || bytes.empty()) {
			return std::size_t{0};
		}

#if defined(_WIN32)
		if (writePending) {
			DWORD sent = 0;
			if (!GetOverlappedResult(handle, &writeOverlapped, &sent, FALSE)) {
				if (GetLastError() == ERROR_IO_INCOMPLETE) {
					return std::size_t{0};
				}

				Abort();
				return std::size_t{0};
			}

			writePending = false;
		}

		std::size_t count = bytes.size() < writeBuffer.size() ? bytes.size() : writeBuffer.size();
		for (std::size_t index = 0; index < count; index++) {
			writeBuffer[index] = bytes[index];
		}

		writeOverlapped = OVERLAPPED{};
		writeOverlapped.hEvent = writeEvent;
		DWORD ignored = 0;
		if (!WriteFile(handle, writeBuffer.data(), static_cast<DWORD>(count), &ignored, &writeOverlapped) && GetLastError() != ERROR_IO_PENDING) {
			Abort();
			return std::size_t{0};
		}

		writePending = true;
		return count;
#else
		ssize_t sent = write(descriptor, bytes.data(), bytes.size());
		if (sent >= 0) {
			return static_cast<std::size_t>(sent);
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			return std::size_t{0};
		}

		Abort();
		return std::size_t{0};
#endif
	}


private:
	// Brings the next read into the read buffer, or finishes the one that is pending.
	void ReadStep() {
#if defined(_WIN32)
		if (!readPending) {
			ResetEvent(readEvent);
			readOverlapped = OVERLAPPED{};
			readOverlapped.hEvent = readEvent;
			if (!ReadFile(handle, readBuffer.data(), static_cast<DWORD>(readBuffer.size()), nullptr, &readOverlapped) && GetLastError() != ERROR_IO_PENDING) {
				Abort();
				return;
			}

			readPending = true;
		}

		DWORD got = 0;
		if (!GetOverlappedResult(handle, &readOverlapped, &got, FALSE)) {
			if (GetLastError() == ERROR_IO_INCOMPLETE) {
				return;
			}

			readPending = false;
			Abort();
			return;
		}

		readPending = false;
		readOffset = 0;
		readAvailable = got;
#else
		ssize_t got = read(descriptor, readBuffer.data(), readBuffer.size());
		if (got > 0) {
			readOffset = 0;
			readAvailable = static_cast<std::size_t>(got);
			return;
		}

		if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
			return;
		}

		Abort();
#endif
	}


	// Closes the port from an I/O path. It cancels the transfers in flight and does not wait: Poll releases the handles once they have
	// completed. Reports down when it was up.
	void Abort() {
		bool wasOpen = portOpen;
		Shut(ReleaseWait::None);
		if (wasOpen) {
			sink.Report(false);
		}
	}


	// Stops the link: no more transfers, and the handles are freed once no transfer is in flight, waiting as the wait allows. Does not report.
	void Shut(ReleaseWait wait) {
		portOpen = false;
		readOffset = 0;
		readAvailable = 0;
		ReleaseLayer layer(*this);
		release.Begin(layer);
		release.Finish(layer, wait);
	}


	// Cancels the transfers in flight. Their completion is seen later through TransfersInFlight.
	void CancelTransfers() {
#if defined(_WIN32)
		if (handle != INVALID_HANDLE_VALUE && (readPending || writePending)) {
			CancelIoEx(handle, nullptr);
		}
#endif
	}


	// Whether a transfer is still in flight when this is called. Never waits.
	bool TransfersInFlight() const {
#if defined(_WIN32)
		bool readWaiting = readPending && !HasOverlappedIoCompleted(&readOverlapped);
		bool writeWaiting = writePending && !HasOverlappedIoCompleted(&writeOverlapped);
		return readWaiting || writeWaiting;
#else
		return false;
#endif
	}


	// Handles the completions that are ready, waiting up to the given time for the transfers in flight (zero does not wait).
	void ProgressTransfers(uint32 milliseconds) {
#if defined(_WIN32)
		HANDLE waitFor[2] = {nullptr, nullptr};
		DWORD count = 0;
		if (readPending && !HasOverlappedIoCompleted(&readOverlapped)) {
			waitFor[count] = readEvent;
			count++;
		}

		if (writePending && !HasOverlappedIoCompleted(&writeOverlapped)) {
			waitFor[count] = writeEvent;
			count++;
		}

		if (count > 0) {
			WaitForMultipleObjects(count, waitFor, TRUE, milliseconds);
		}
#else
		static_cast<void>(milliseconds);
#endif
	}


	// Frees the port and its handles. Called only when no transfer is in flight.
	void FreeHandles() {
#if defined(_WIN32)
		if (handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
		}

		if (readEvent != nullptr) {
			CloseHandle(readEvent);
			readEvent = nullptr;
		}

		if (writeEvent != nullptr) {
			CloseHandle(writeEvent);
			writeEvent = nullptr;
		}

		readPending = false;
		writePending = false;
#else
		if (descriptor >= 0) {
			::close(descriptor);
			descriptor = -1;
		}
#endif
	}


#if defined(_WIN32)
	static ResultStatus ErrorOf(DWORD error) {
		if (error == ERROR_FILE_NOT_FOUND) {
			return ResultStatus::notFound;
		}

		if (error == ERROR_ACCESS_DENIED) {
			return ResultStatus::deviceOrResourceBusy;
		}

		return ResultStatus::ioError;
	}
#else
	static ResultStatus ErrorOf(int error) {
		if (error == ENOENT) {
			return ResultStatus::notFound;
		}

		if (error == EACCES || error == EBUSY) {
			return ResultStatus::deviceOrResourceBusy;
		}

		return ResultStatus::ioError;
	}


	// The termios speed of a baud rate. False for a rate that termios does not have here.
	static bool SpeedOf(std::uint32_t baud, speed_t& speed) {
		const std::array<std::uint32_t, 11> rates{{1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600}};
		const std::array<speed_t, 11> speeds{{B1200, B2400, B4800, B9600, B19200, B38400, B57600, B115200, B230400, B460800, B921600}};
		for (std::size_t index = 0; index < rates.size(); index++) {
			if (rates[index] == baud) {
				speed = speeds[index];
				return true;
			}
		}

		return false;
	}
#endif
};

}
