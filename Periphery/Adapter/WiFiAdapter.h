#pragma once
#include "IAdapter.h"
#include <Adapter/Utilities/NetworkTypes.h>

#define VHAL_WIFI_ADAPTER


// Wi-Fi link control: scanning, joining an access point, running an access point.
// It carries no data. TCP/UDP/TLS go through SocketAdapter, so the same application code runs over any link.
//
// The class is not a template: a board can hold a built-in radio and an external module at once, and portable code
// takes a WiFiAdapter& without knowing which one it got.
class WiFiAdapter : public IAdapter {
public:
	static constexpr uint8 maxSsidLength = 32;
	static constexpr uint8 maxPasswordLength = 64;
	static constexpr uint8 minPasswordLength = 8;

	enum class Interface { Station, AccessPoint };
	enum class Security { Open, WEP, WPA, WPA2, WPA2WPA3, WPA3, Enterprise, Unknown };
	enum class PowerSave { None, Minimum, Maximum };

	// Station state. Linked: associated with the access point. Ready: the station also has an IP address.
	enum class State { Stopped, Idle, Connecting, Linked, Ready };

	enum class Event {
		Connected, Disconnected, GotIp, LostIp,
		AccessPointStarted, AccessPointStopped, ClientConnected, ClientDisconnected
	};

	enum class DisconnectReason { None, Requested, AuthenticationFailed, NotFound, ConnectionLost, Unknown };


	struct Parameters {
		PowerSave powerSave = PowerSave::Minimum;
		const char *hostname = nullptr;		// nullptr keeps the port default
	};

	struct StationConfig {
		const char *ssid = nullptr;
		const char *password = nullptr;			// nullptr or "" joins an open network
		const MacAddress *bssid = nullptr;		// Set to join one specific access point
		// Weakest security accepted when a password is given; it is ignored for an open network.
		Security minSecurity = Security::WPA2;
	};

	struct AccessPointConfig {
		const char *ssid = nullptr;
		const char *password = nullptr;			// nullptr or "" starts an open access point
		uint8 channel = 1;
		uint8 maxClients = 4;
		bool hidden = false;
	};

	struct AccessPointInfo {
		char ssid[maxSsidLength + 1] = {};
		MacAddress bssid;
		int8 rssi = 0;							// dBm
		uint8 channel = 0;
		Security security = Security::Unknown;
	};

	struct IpInfo {
		Ipv4Address address;
		Ipv4Address mask;
		Ipv4Address gateway;
	};


protected:
	Parameters parameters;
	uint32 timeout = 15000;
	volatile State state = State::Stopped;
	volatile DisconnectReason lastDisconnectReason = DisconnectReason::None;


public:
	// Runs in the context of the radio stack (its own task or Process()), not in the caller's context.
	// Do not block in it and do not call Connect() or Scan() from it; signal a thread instead.
	std::function<void(Event event)> onEvent;


public:
	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	// Limit for the blocking Connect(), in milliseconds.
	virtual void SetTimeout(uint32 val) {
		timeout = val;
	}


	inline State GetState() const {
		return state;
	}


	inline DisconnectReason GetLastDisconnectReason() const {
		return lastDisconnectReason;
	}


	// Blocks until the station is Ready, the attempt fails or the timeout expires.
	// The adapter does not reconnect on its own: on Event::Disconnected the application decides what to do.
	virtual ResultStatus Connect(const StationConfig& config) {
		ResultStatus status = ConnectAsync(config);
		if (status != ResultStatus::ok) {
			return status;
		}

		uint64 start = System::GetMs();
		while (state != State::Ready) {
			if (state == State::Idle || state == State::Stopped) {
				return DisconnectReasonToStatus(lastDisconnectReason);
			}

			if (System::GetMs() - start >= timeout) {
				Disconnect();
				return ResultStatus::timeout;
			}

			Process();
			System::DelayMs(10);
		}

		return ResultStatus::ok;
	}


	// Starts the attempt and returns; the outcome arrives through onEvent and GetState().
	virtual ResultStatus ConnectAsync(const StationConfig& config) = 0;
	virtual ResultStatus Disconnect() = 0;

	// Blocking. Calls onResult once per access point found and returns their number.
	virtual Result<uint16> Scan(const std::function<void(const AccessPointInfo& info)>& onResult) = 0;

	virtual ResultStatus StartAccessPoint(const AccessPointConfig& config) = 0;
	virtual ResultStatus StopAccessPoint() = 0;

	// Turns the radio off. SetParameters() turns it on again.
	virtual ResultStatus Stop() = 0;

	virtual Result<int8> GetRssi() = 0;									// dBm of the joined access point
	virtual Result<MacAddress> GetMacAddress(Interface interface = Interface::Station) = 0;
	virtual Result<IpInfo> GetIpInfo(Interface interface = Interface::Station) = 0;

	virtual ResultStatus SetPowerSave(PowerSave val) = 0;
	virtual ResultStatus SetTxPower(int8 dBm) = 0;

	// Drives radios that have no task of their own (external chips polled by the application).
	// Ports with their own task leave it empty; calling it is always safe.
	virtual void Process() { }


protected:
	virtual ResultStatus Initialization() = 0;


	virtual inline void CallEvent(Event event) {
		if (onEvent != nullptr) {
			onEvent(event);
		}
	}


	static constexpr ResultStatus DisconnectReasonToStatus(DisconnectReason reason) {
		switch (reason) {
			case DisconnectReason::AuthenticationFailed:
				return ResultStatus::authenticationFailed;

			case DisconnectReason::NotFound:
				return ResultStatus::notFound;

			case DisconnectReason::ConnectionLost:
				return ResultStatus::signalLost;

			default:
				return ResultStatus::noConnection;
		}
	}


	static constexpr bool IsStringLengthValid(const char *string, uint32 minLength, uint32 maxLength) {
		if (string == nullptr) {
			return false;
		}

		uint32 length = 0;
		while (string[length] != '\0') {
			length++;
			if (length > maxLength) {
				return false;
			}
		}

		return length >= minLength;
	}


	static constexpr bool IsEmpty(const char *string) {
		return string == nullptr || string[0] == '\0';
	}
};
