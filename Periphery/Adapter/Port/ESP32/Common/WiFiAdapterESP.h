#pragma once
#include <Adapter/WiFiAdapter.h>
#include <cstring>


using AWIFI = class WiFiAdapterESP;


// Built-in Wi-Fi radio of the ESP32 family, on top of the ESP-IDF esp_wifi driver.
// The chip has one radio, so the application holds one instance.
//
// The station interface stays enabled the whole time, which keeps Scan() available while an access point runs.
// Credentials are kept in RAM only: the driver's own NVS storage is not used, saving them is the application's job.
class WiFiAdapterESP : public WiFiAdapter {
protected:
	esp_netif_t *stationNetif = nullptr;
	esp_netif_t *accessPointNetif = nullptr;
	esp_event_handler_instance_t wifiEventInstance = nullptr;
	esp_event_handler_instance_t ipEventInstance = nullptr;
	bool driverInitialized = false;
	bool accessPointActive = false;


public:
	WiFiAdapterESP() = default;

	WiFiAdapterESP(const WiFiAdapterESP&) = delete;
	WiFiAdapterESP& operator=(const WiFiAdapterESP&) = delete;


	ResultStatus ConnectAsync(const StationConfig& config) override {
		if (state == State::Stopped) {
			return ResultStatus::noInit;
		}

		if (!IsStringLengthValid(config.ssid, 1, maxSsidLength)) {
			return ResultStatus::invalidParameter;
		}

		bool isOpen = IsEmpty(config.password);
		if (!isOpen && !IsStringLengthValid(config.password, minPasswordLength, maxPasswordLength)) {
			return ResultStatus::invalidParameter;
		}

		wifi_config_t wifiConfig = {};
		std::memcpy(wifiConfig.sta.ssid, config.ssid, std::strlen(config.ssid));
		if (!isOpen) {
			// A 64-character password fills the field without a terminator, which the driver accepts.
			std::memcpy(wifiConfig.sta.password, config.password, std::strlen(config.password));
			wifiConfig.sta.threshold.authmode = CastSecurity(config.minSecurity);
		} else {
			wifiConfig.sta.threshold.authmode = WIFI_AUTH_OPEN;
		}

		if (config.bssid != nullptr) {
			wifiConfig.sta.bssid_set = true;
			std::memcpy(wifiConfig.sta.bssid, config.bssid->bytes, MacAddress::size);
		}

		// A join that is still running has to be dropped first, otherwise esp_wifi_connect() reports it as busy.
		if (state != State::Idle) {
			esp_wifi_disconnect();
		}

		if (esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) != ESP_OK) {
			return ResultStatus::invalidParameter;
		}

		lastDisconnectReason = DisconnectReason::None;
		state = State::Connecting;

		if (esp_wifi_connect() != ESP_OK) {
			state = State::Idle;
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	ResultStatus Disconnect() override {
		if (state == State::Stopped) {
			return ResultStatus::noInit;
		}

		if (esp_wifi_disconnect() != ESP_OK) {
			return ResultStatus::error;
		}

		// The driver raises no event when nothing was joined, so the state is settled here as well.
		lastDisconnectReason = DisconnectReason::Requested;
		state = State::Idle;
		return ResultStatus::ok;
	}


	Result<uint16> Scan(const std::function<void(const AccessPointInfo& info)>& onResult) override {
		if (state == State::Stopped) {
			return ResultStatus::noInit;
		}

		// The radio cannot scan while it is joining an access point.
		if (state == State::Connecting) {
			return ResultStatus::busy;
		}

		if (esp_wifi_scan_start(nullptr, true) != ESP_OK) {
			return ResultStatus::error;
		}

		// Records are taken from the driver one at a time, so no list has to be allocated here.
		uint16 count = 0;
		wifi_ap_record_t record = {};
		while (esp_wifi_scan_get_ap_record(&record) == ESP_OK) {
			count++;

			if (onResult != nullptr) {
				AccessPointInfo info = {};
				std::memcpy(info.ssid, record.ssid, maxSsidLength);
				std::memcpy(info.bssid.bytes, record.bssid, MacAddress::size);
				info.rssi = record.rssi;
				info.channel = record.primary;
				info.security = CastAuthMode(record.authmode);
				onResult(info);
			}
		}

		esp_wifi_clear_ap_list();
		return count;
	}


	ResultStatus StartAccessPoint(const AccessPointConfig& config) override {
		if (state == State::Stopped) {
			return ResultStatus::noInit;
		}

		if (!IsStringLengthValid(config.ssid, 1, maxSsidLength)) {
			return ResultStatus::invalidParameter;
		}

		bool isOpen = IsEmpty(config.password);
		// The access point password field needs room for its terminator, so 64 characters do not fit.
		if (!isOpen && !IsStringLengthValid(config.password, minPasswordLength, maxPasswordLength - 1)) {
			return ResultStatus::invalidParameter;
		}

		if (config.channel == 0 || config.maxClients == 0) {
			return ResultStatus::invalidParameter;
		}

		wifi_config_t wifiConfig = {};
		uint8 ssidLength = static_cast<uint8>(std::strlen(config.ssid));
		std::memcpy(wifiConfig.ap.ssid, config.ssid, ssidLength);
		wifiConfig.ap.ssid_len = ssidLength;
		wifiConfig.ap.channel = config.channel;
		wifiConfig.ap.max_connection = config.maxClients;
		wifiConfig.ap.ssid_hidden = config.hidden ? 1 : 0;

		if (!isOpen) {
			std::memcpy(wifiConfig.ap.password, config.password, std::strlen(config.password));
			wifiConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
		} else {
			wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
		}

		if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
			return ResultStatus::error;
		}

		if (esp_wifi_set_config(WIFI_IF_AP, &wifiConfig) != ESP_OK) {
			esp_wifi_set_mode(WIFI_MODE_STA);
			return ResultStatus::invalidParameter;
		}

		accessPointActive = true;
		return ResultStatus::ok;
	}


	ResultStatus StopAccessPoint() override {
		if (state == State::Stopped) {
			return ResultStatus::noInit;
		}

		if (!accessPointActive) {
			return ResultStatus::ok;
		}

		if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
			return ResultStatus::error;
		}

		accessPointActive = false;
		return ResultStatus::ok;
	}


	ResultStatus Stop() override {
		if (state == State::Stopped) {
			return ResultStatus::ok;
		}

		if (esp_wifi_stop() != ESP_OK) {
			return ResultStatus::error;
		}

		accessPointActive = false;
		state = State::Stopped;
		return ResultStatus::ok;
	}


	Result<int8> GetRssi() override {
		if (state != State::Linked && state != State::Ready) {
			return ResultStatus::noConnection;
		}

		wifi_ap_record_t record = {};
		if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) {
			return ResultStatus::noConnection;
		}

		return static_cast<int8>(record.rssi);
	}


	Result<MacAddress> GetMacAddress(Interface interface = Interface::Station) override {
		if (!driverInitialized) {
			return ResultStatus::noInit;
		}

		MacAddress address;
		if (esp_wifi_get_mac(CastInterface(interface), address.bytes) != ESP_OK) {
			return ResultStatus::error;
		}

		return address;
	}


	Result<IpInfo> GetIpInfo(Interface interface = Interface::Station) override {
		esp_netif_t *netif = (interface == Interface::Station) ? stationNetif : accessPointNetif;
		if (netif == nullptr) {
			return ResultStatus::noInit;
		}

		esp_netif_ip_info_t ipInfo = {};
		if (esp_netif_get_ip_info(netif, &ipInfo) != ESP_OK) {
			return ResultStatus::error;
		}

		IpInfo info;
		info.address = CastIp(ipInfo.ip);
		info.mask = CastIp(ipInfo.netmask);
		info.gateway = CastIp(ipInfo.gw);
		return info;
	}


	ResultStatus SetPowerSave(PowerSave val) override {
		if (!driverInitialized) {
			return ResultStatus::noInit;
		}

		if (esp_wifi_set_ps(CastPowerSave(val)) != ESP_OK) {
			return ResultStatus::error;
		}

		parameters.powerSave = val;
		return ResultStatus::ok;
	}


	// The radio accepts 2 to 20 dBm and rounds down to a level it supports. Valid only while the radio is started.
	ResultStatus SetTxPower(int8 dBm) override {
		if (state == State::Stopped) {
			return ResultStatus::noInit;
		}

		if (dBm < 2 || dBm > 20) {
			return ResultStatus::outOfRange;
		}

		// The driver counts in units of 0.25 dBm.
		if (esp_wifi_set_max_tx_power(static_cast<int8_t>(dBm * 4)) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


protected:
	ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		status = InitDriver();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (parameters.hostname != nullptr && esp_netif_set_hostname(stationNetif, parameters.hostname) != ESP_OK) {
			return ResultStatus::invalidParameter;
		}

		if (state == State::Stopped) {
			if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
				return ResultStatus::error;
			}

			if (esp_wifi_start() != ESP_OK) {
				return ResultStatus::error;
			}

			lastDisconnectReason = DisconnectReason::None;
			state = State::Idle;
		}

		if (esp_wifi_set_ps(CastPowerSave(parameters.powerSave)) != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


private:
	// Runs once. Everything here is shared with other ESP-IDF users of the network stack, so "already done" is not
	// an error.
	ResultStatus InitDriver() {
		if (driverInitialized) {
			return ResultStatus::ok;
		}

		// NVS only speeds up radio calibration at boot. It is never erased here: if it cannot be opened the radio
		// calibrates from scratch, and repairing the partition is the application's decision.
		nvs_flash_init();

		esp_err_t result = esp_netif_init();
		if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
			return ResultStatus::error;
		}

		result = esp_event_loop_create_default();
		if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
			return ResultStatus::error;
		}

		if (stationNetif == nullptr) {
			stationNetif = esp_netif_create_default_wifi_sta();
		}

		if (accessPointNetif == nullptr) {
			accessPointNetif = esp_netif_create_default_wifi_ap();
		}

		if (stationNetif == nullptr || accessPointNetif == nullptr) {
			return ResultStatus::outOfMemory;
		}

		wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
		initConfig.nvs_enable = 0;
		if (esp_wifi_init(&initConfig) != ESP_OK) {
			return ResultStatus::error;
		}

		if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
			return ResultStatus::error;
		}

		if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, EventHandler, this, &wifiEventInstance) != ESP_OK) {
			return ResultStatus::error;
		}

		if (esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, EventHandler, this, &ipEventInstance) != ESP_OK) {
			return ResultStatus::error;
		}

		driverInitialized = true;
		return ResultStatus::ok;
	}


	// Called from the ESP-IDF default event loop task.
	static void EventHandler(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
		WiFiAdapterESP *adapter = static_cast<WiFiAdapterESP *>(argument);

		if (eventBase == WIFI_EVENT) {
			adapter->HandleWifiEvent(eventId, eventData);
		} else if (eventBase == IP_EVENT) {
			adapter->HandleIpEvent(eventId);
		}
	}


	void HandleWifiEvent(int32_t eventId, void *eventData) {
		switch (eventId) {
			case WIFI_EVENT_STA_CONNECTED:
				state = State::Linked;
				CallEvent(Event::Connected);
			break;

			case WIFI_EVENT_STA_DISCONNECTED: {
				// A Disconnect() call has already settled the state and the reason.
				if (state == State::Idle || state == State::Stopped) {
					break;
				}

				wifi_event_sta_disconnected_t *info = static_cast<wifi_event_sta_disconnected_t *>(eventData);

				// ConnectAsync() drops the previous link first; that late event must not fail the new attempt.
				if (state == State::Connecting && info->reason == WIFI_REASON_ASSOC_LEAVE) {
					break;
				}

				lastDisconnectReason = CastDisconnectReason(info->reason);
				state = State::Idle;
				CallEvent(Event::Disconnected);
			}
			break;

			case WIFI_EVENT_AP_START:
				CallEvent(Event::AccessPointStarted);
			break;

			case WIFI_EVENT_AP_STOP:
				CallEvent(Event::AccessPointStopped);
			break;

			case WIFI_EVENT_AP_STACONNECTED:
				CallEvent(Event::ClientConnected);
			break;

			case WIFI_EVENT_AP_STADISCONNECTED:
				CallEvent(Event::ClientDisconnected);
			break;

			default:
			break;
		}
	}


	void HandleIpEvent(int32_t eventId) {
		switch (eventId) {
			case IP_EVENT_STA_GOT_IP:
				if (state == State::Linked) {
					state = State::Ready;
				}
				CallEvent(Event::GotIp);
			break;

			case IP_EVENT_STA_LOST_IP:
				if (state == State::Ready) {
					state = State::Linked;
				}
				CallEvent(Event::LostIp);
			break;

			default:
			break;
		}
	}


	static constexpr DisconnectReason CastDisconnectReason(uint8 reason) {
		switch (reason) {
			case WIFI_REASON_AUTH_EXPIRE:
			case WIFI_REASON_AUTH_FAIL:
			case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
			case WIFI_REASON_HANDSHAKE_TIMEOUT:
				return DisconnectReason::AuthenticationFailed;

			case WIFI_REASON_NO_AP_FOUND:
				return DisconnectReason::NotFound;

			case WIFI_REASON_BEACON_TIMEOUT:
				return DisconnectReason::ConnectionLost;

			case WIFI_REASON_ASSOC_LEAVE:
				return DisconnectReason::Requested;

			default:
				return DisconnectReason::Unknown;
		}
	}


	static constexpr wifi_auth_mode_t CastSecurity(Security security) {
		switch (security) {
			case Security::Open:
				return WIFI_AUTH_OPEN;

			case Security::WEP:
				return WIFI_AUTH_WEP;

			case Security::WPA:
				return WIFI_AUTH_WPA_PSK;

			case Security::WPA2WPA3:
				return WIFI_AUTH_WPA2_WPA3_PSK;

			case Security::WPA3:
				return WIFI_AUTH_WPA3_PSK;

			case Security::Enterprise:
				return WIFI_AUTH_WPA2_ENTERPRISE;

			case Security::WPA2:
			default:
				return WIFI_AUTH_WPA2_PSK;
		}
	}


	static constexpr Security CastAuthMode(wifi_auth_mode_t authMode) {
		switch (authMode) {
			case WIFI_AUTH_OPEN:
				return Security::Open;

			case WIFI_AUTH_WEP:
				return Security::WEP;

			case WIFI_AUTH_WPA_PSK:
				return Security::WPA;

			case WIFI_AUTH_WPA2_PSK:
			case WIFI_AUTH_WPA_WPA2_PSK:
				return Security::WPA2;

			case WIFI_AUTH_WPA2_WPA3_PSK:
				return Security::WPA2WPA3;

			case WIFI_AUTH_WPA3_PSK:
				return Security::WPA3;

			case WIFI_AUTH_WPA2_ENTERPRISE:
				return Security::Enterprise;

			default:
				return Security::Unknown;
		}
	}


	static constexpr wifi_ps_type_t CastPowerSave(PowerSave powerSave) {
		switch (powerSave) {
			case PowerSave::None:
				return WIFI_PS_NONE;

			case PowerSave::Maximum:
				return WIFI_PS_MAX_MODEM;

			case PowerSave::Minimum:
			default:
				return WIFI_PS_MIN_MODEM;
		}
	}


	static constexpr wifi_interface_t CastInterface(Interface interface) {
		return (interface == Interface::Station) ? WIFI_IF_STA : WIFI_IF_AP;
	}


	// esp_ip4_addr_t keeps the address in network byte order, so its bytes in memory are the octets as written.
	static Ipv4Address CastIp(const esp_ip4_addr_t& address) {
		Ipv4Address result;
		std::memcpy(result.bytes, &address.addr, Ipv4Address::size);
		return result;
	}
};
