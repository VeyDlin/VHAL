#pragma once
#include <Adapter/BLEAdapter.h>
#include <cstring>


using ABLE = class BLEAdapterESP;


// Bluetooth Low Energy on the ESP32 built-in radio, on the NimBLE host.
// The radio is one, so the application holds one instance. The NimBLE callbacks that carry no user argument (reset and
// sync) reach the adapter through instance.
//
// The NimBLE tables point at the Service and Characteristic objects of the application, so those objects must outlive
// the radio. The tables are built once, before the host starts.
//
// Stop() is not supported. The host is not restarted, which would also mean rebuilding the GATT table; that restart
// path has not been verified on this port. After a failed start the radio is left initialised and is not retried.
class BLEAdapterESP : public BLEAdapter {
public:
	static constexpr uint8 maxServices = 8;
	static constexpr uint8 maxCharacteristics = 32;
	static constexpr uint8 maxAdvertisedUuids = 8;
	static constexpr uint8 maxSubscriptions = 16;
	static constexpr uint16 minIntervalMs = 20;
	static constexpr uint16 maxIntervalMs = 10240;


protected:
	ble_gatt_svc_def serviceDefs[maxServices + 1] = {};
	ble_gatt_chr_def characteristicDefs[maxCharacteristics + maxServices] = {};
	ble_uuid_any_t uuids[maxServices + maxCharacteristics] = {};
	uint8 ownAddressType = BLE_OWN_ADDR_PUBLIC;
	std::function<void(const ScanResult& result)> scanCallback;


private:
	// One peer's subscription to one value handle. Only subscriptions with notify or indicate set are kept.
	struct Subscription {
		ConnectionHandle connection;
		uint16 valueHandle;
		bool notify;
		bool indicate;
	};


	static constexpr uint32 hostSyncTimeoutMs = 5000;

	// The longest value a notification can carry: the ATT attribute length limit.
	static constexpr uint16 maxValueSize = BLE_ATT_ATTR_MAX_LEN;

	// The power levels of the ESP32 controller, from -12 dBm (index 0) to +9 dBm (index 7), in 3 dBm steps.
	static constexpr int8 txPowerDbm[8] = { -12, -9, -6, -3, 0, 3, 6, 9 };

	volatile bool hostSynced = false;
	static inline BLEAdapterESP *instance = nullptr;

	// Written in the host task and read by Notify() in the application task. Every access is inside a critical section.
	Subscription subscriptions[maxSubscriptions] = {};
	uint8 subscriptionCount = 0;

	// Subscriptions that did not fit into the table. Notify() does not reach those peers. The other peers are unaffected.
	uint32 refusedSubscriptions = 0;


public:
	BLEAdapterESP() {
		instance = this;
	}

	BLEAdapterESP(const BLEAdapterESP&) = delete;
	BLEAdapterESP& operator=(const BLEAdapterESP&) = delete;

	~BLEAdapterESP() {
		if (instance == this) {
			instance = nullptr;
		}
	}


	// The radio cannot be reconfigured once started, so the stored parameters are left as the radio uses them.
	ResultStatus SetParameters(Parameters val) override {
		if (started) {
			return ResultStatus::busy;
		}

		return BLEAdapter::SetParameters(val);
	}


	// Not supported in this version: see the note on the class.
	ResultStatus Stop() override {
		return ResultStatus::notSupported;
	}


	ResultStatus StartAdvertising(const AdvertisingConfig& config) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		if (config.intervalMinMs < minIntervalMs || config.intervalMaxMs > maxIntervalMs || config.intervalMinMs > config.intervalMaxMs) {
			return ResultStatus::invalidParameter;
		}

		if (config.serviceUuidCount > maxAdvertisedUuids) {
			return ResultStatus::outOfRange;
		}

		if (config.serviceUuidCount > 0 && config.serviceUuids == nullptr) {
			return ResultStatus::invalidParameter;
		}

		if (config.manufacturerDataSize > 0 && config.manufacturerData == nullptr) {
			return ResultStatus::invalidParameter;
		}

		// NimBLE copies the fields into its own buffer, so these arrays only need to live through the calls below.
		ble_uuid16_t uuids16[maxAdvertisedUuids] = {};
		ble_uuid128_t uuids128[maxAdvertisedUuids] = {};
		ble_hs_adv_fields fields = {};
		fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

		for (uint8 i = 0; i < config.serviceUuidCount; i++) {
			const Uuid& uuid = config.serviceUuids[i];
			if (uuid.size == 2) {
				uuids16[fields.num_uuids16] = ToNimble16(uuid);
				fields.num_uuids16++;
			} else if (uuid.size == Uuid::maxSize) {
				uuids128[fields.num_uuids128] = ToNimble128(uuid);
				fields.num_uuids128++;
			} else {
				return ResultStatus::invalidParameter;
			}
		}

		fields.uuids16 = uuids16;
		fields.uuids16_is_complete = 1;
		fields.uuids128 = uuids128;
		fields.uuids128_is_complete = 1;
		fields.mfg_data = config.manufacturerData;
		fields.mfg_data_len = config.manufacturerDataSize;

		// The name goes into the advertisement when it fits, otherwise into the scan response.
		bool nameRequested = config.includeName && parameters.deviceName != nullptr;
		size_t nameLength = nameRequested ? std::strlen(parameters.deviceName) : 0;
		if (nameLength > 0xFF) {
			return ResultStatus::invalidParameter;
		}

		uint8 nameSize = static_cast<uint8>(nameLength);
		if (nameRequested) {
			fields.name = reinterpret_cast<const uint8 *>(parameters.deviceName);
			fields.name_len = nameSize;
			fields.name_is_complete = 1;
		}

		ble_hs_adv_fields response = {};
		int rc = ble_gap_adv_set_fields(&fields);
		if (rc == BLE_HS_EMSGSIZE && nameRequested) {
			fields.name = nullptr;
			fields.name_len = 0;
			fields.name_is_complete = 0;
			rc = ble_gap_adv_set_fields(&fields);

			response.name = reinterpret_cast<const uint8 *>(parameters.deviceName);
			response.name_len = nameSize;
			response.name_is_complete = 1;
		}

		if (rc == 0) {
			rc = ble_gap_adv_rsp_set_fields(&response);
		}

		if (rc != 0) {
			return MapError(rc);
		}

		ble_gap_adv_params params = {};
		params.conn_mode = config.connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
		params.disc_mode = BLE_GAP_DISC_MODE_GEN;
		params.itvl_min = MsToUnits(config.intervalMinMs);
		params.itvl_max = MsToUnits(config.intervalMaxMs);

		rc = ble_gap_adv_start(ownAddressType, nullptr, Duration(config.durationMs), &params, OnGapEvent, this);
		return MapError(rc);
	}


	ResultStatus StopAdvertising() override {
		if (!started) {
			return ResultStatus::noInit;
		}

		if (ble_gap_adv_active() == 0) {
			return ResultStatus::ok;
		}

		return MapError(ble_gap_adv_stop());
	}


	ResultStatus Notify(Characteristic& characteristic) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		if (characteristic.valueHandle == 0) {
			return ResultStatus::notExist;
		}

		uint16 length = characteristic.GetLength();
		if (length > maxValueSize) {
			return ResultStatus::outOfRange;
		}

		Subscription targets[maxSubscriptions] = {};
		uint8 targetCount = CopySubscriptions(characteristic.valueHandle, targets);
		if (targetCount == 0) {
			return ResultStatus::ok;
		}

		// One copy for every peer, so they all get the same bytes. NimBLE reads from the copy only after this returns.
		uint8 snapshot[maxValueSize] = {};
		if (length > 0) {
			std::memcpy(snapshot, characteristic.GetValue(), length);
		}

		// Every peer is tried. The first failure is returned, so one failing peer does not stop the others.
		ResultStatus result = ResultStatus::ok;
		for (uint8 i = 0; i < targetCount; i++) {
			ResultStatus status = SendTo(targets[i], snapshot, length);
			if (status != ResultStatus::ok && result == ResultStatus::ok) {
				result = status;
			}
		}

		return result;
	}


	ResultStatus Disconnect(ConnectionHandle connection) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		return MapError(ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM));
	}


	Result<int8> GetRssi(ConnectionHandle connection) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		int8 rssi = 0;
		int rc = ble_gap_conn_rssi(connection, &rssi);
		if (rc != 0) {
			return MapError(rc);
		}

		return rssi;
	}


	Result<uint16> GetMtu(ConnectionHandle connection) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		uint16 mtu = ble_att_mtu(connection);
		if (mtu == 0) {
			return ResultStatus::noConnection;
		}

		return mtu;
	}


	Result<Address> GetAddress() override {
		if (!started) {
			return ResultStatus::noInit;
		}

		// The own address types 0 and 1 are the same values as the identity address types.
		uint8 identityType = (ownAddressType == BLE_OWN_ADDR_RANDOM) ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
		uint8 value[MacAddress::size] = {};
		int rc = ble_hs_id_copy_addr(identityType, value, nullptr);
		if (rc != 0) {
			return MapError(rc);
		}

		Address result;
		result.address = ToMacAddress(value);
		result.type = (identityType == BLE_ADDR_RANDOM) ? AddressType::Random : AddressType::Public;
		return result;
	}


	Result<Address> GetPeerAddress(ConnectionHandle connection) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		ble_gap_conn_desc desc = {};
		int rc = ble_gap_conn_find(connection, &desc);
		if (rc != 0) {
			return MapError(rc);
		}

		return ToAddress(desc.peer_id_addr);
	}


	// Scanning uses the own address type of the started radio. Only one scan runs at a time.
	ResultStatus StartScan(const ScanConfig& config, const std::function<void(const ScanResult& result)>& onResult) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		if (onResult == nullptr) {
			return ResultStatus::invalidParameter;
		}

		if (config.intervalMs == 0 || config.windowMs == 0 || config.windowMs > config.intervalMs || config.intervalMs > maxIntervalMs) {
			return ResultStatus::invalidParameter;
		}

		if (ble_gap_disc_active() != 0) {
			return ResultStatus::busy;
		}

		ble_gap_disc_params params = {};
		params.itvl = MsToUnits(config.intervalMs);
		params.window = MsToUnits(config.windowMs);
		params.passive = config.active ? 0 : 1;
		params.filter_duplicates = config.filterDuplicates ? 1 : 0;

		// Set before the scan starts: reports can arrive in the host task before ble_gap_disc() returns.
		scanCallback = onResult;
		int rc = ble_gap_disc(ownAddressType, Duration(config.durationMs), &params, OnGapEvent, this);
		if (rc != 0) {
			scanCallback = nullptr;
		}

		return MapError(rc);
	}


	ResultStatus StopScan() override {
		if (!started) {
			return ResultStatus::noInit;
		}

		if (ble_gap_disc_active() == 0) {
			return ResultStatus::ok;
		}

		return MapError(ble_gap_disc_cancel());
	}


	// The controller takes eight levels, -12 to +9 dBm. The highest level not above the request is used, so a request
	// above +9 dBm selects +9 dBm, and a request below -12 dBm is out of range.
	ResultStatus SetTxPower(int8 dBm) override {
		if (!started) {
			return ResultStatus::noInit;
		}

		int8 selected = -1;
		for (uint8 i = 0; i < 8; i++) {
			if (txPowerDbm[i] <= dBm) {
				selected = static_cast<int8>(i);
			}
		}

		if (selected < 0) {
			return ResultStatus::outOfRange;
		}

		esp_err_t result = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, static_cast<esp_power_level_t>(selected));
		return (result == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


protected:
	ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (started) {
			return ResultStatus::busy;
		}

		// Only memory is touched here, so a table that is too large is refused before the radio is initialised.
		status = BuildGattTable();
		if (status != ResultStatus::ok) {
			return status;
		}

		// NVS only speeds up radio calibration at boot. It is never erased here, same as in the Wi-Fi port.
		nvs_flash_init();

		if (nimble_port_init() != ESP_OK) {
			return ResultStatus::error;
		}

		ble_hs_cfg.reset_cb = OnReset;
		ble_hs_cfg.sync_cb = OnSync;
		ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
		ble_hs_cfg.sm_io_cap = CastIoCapability(parameters.ioCapability);
		ble_hs_cfg.sm_bonding = parameters.bonding;
		ble_hs_cfg.sm_mitm = parameters.mitm;
		ble_hs_cfg.sm_sc = parameters.secureConnections;

		if (parameters.bonding) {
			ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
			ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
		}

		ble_svc_gap_init();
		ble_svc_gatt_init();

		int rc = ble_gatts_count_cfg(serviceDefs);
		if (rc == 0) {
			rc = ble_gatts_add_svcs(serviceDefs);
		}

		if (rc != 0) {
			return MapError(rc);
		}

		if (parameters.deviceName != nullptr) {
			rc = ble_svc_gap_device_name_set(parameters.deviceName);
			if (rc != 0) {
				return MapError(rc);
			}
		}

		rc = ble_svc_gap_device_appearance_set(parameters.appearance);
		if (rc != 0) {
			return MapError(rc);
		}

		nimble_port_freertos_init(HostTask);

		uint64 start = System::GetMs();
		while (!hostSynced) {
			if (System::GetMs() - start >= hostSyncTimeoutMs) {
				return ResultStatus::timeout;
			}

			System::DelayMs(10);
		}

		rc = ble_hs_id_infer_auto(0, &ownAddressType);
		if (rc != 0) {
			return MapError(rc);
		}

		started = true;
		return AfterInitialization();
	}


private:
	// Fills the NimBLE tables from the registered services. Each service's characteristics end with a zero entry,
	// which is the NimBLE terminator; the array sizes leave room for every terminator.
	ResultStatus BuildGattTable() {
		uint8 serviceCount = 0;
		uint8 characteristicCount = 0;
		uint8 uuidCount = 0;

		for (Service *service = services; service != nullptr; service = service->next) {
			if (serviceCount == maxServices) {
				return ResultStatus::outOfRange;
			}

			ResultStatus status = StoreUuid(service->uuid, uuids[uuidCount]);
			if (status != ResultStatus::ok) {
				return status;
			}

			ble_gatt_svc_def& definition = serviceDefs[serviceCount];
			definition.type = BLE_GATT_SVC_TYPE_PRIMARY;
			definition.uuid = &uuids[uuidCount].u;
			definition.includes = nullptr;
			definition.characteristics = &characteristicDefs[characteristicCount];
			uuidCount++;

			for (Characteristic *characteristic = service->characteristics; characteristic != nullptr; characteristic = characteristic->next) {
				if (characteristicCount == maxCharacteristics) {
					return ResultStatus::outOfRange;
				}

				status = StoreUuid(characteristic->uuid, uuids[uuidCount]);
				if (status != ResultStatus::ok) {
					return status;
				}

				ble_gatt_chr_def& chr = characteristicDefs[characteristicCount];
				chr.uuid = &uuids[uuidCount].u;
				chr.access_cb = OnGattAccess;
				chr.arg = characteristic;
				chr.flags = CastFlags(*characteristic);
				chr.min_key_size = 0;
				chr.val_handle = &characteristic->valueHandle;
				chr.descriptors = nullptr;
				chr.cpfd = nullptr;
				uuidCount++;
				characteristicCount++;
			}

			characteristicDefs[characteristicCount] = ble_gatt_chr_def{};
			characteristicCount++;
			serviceCount++;
		}

		serviceDefs[serviceCount] = ble_gatt_svc_def{};
		return ResultStatus::ok;
	}


	// Called in the NimBLE host task.
	static int OnGattAccess(uint16 connection, uint16 attribute, ble_gatt_access_ctxt *context, void *argument) {
		(void)attribute;
		Characteristic *characteristic = static_cast<Characteristic *>(argument);

		switch (context->op) {
			case BLE_GATT_ACCESS_OP_READ_CHR:
				return HandleRead(connection, characteristic, context->om);

			case BLE_GATT_ACCESS_OP_WRITE_CHR:
				return HandleWrite(connection, characteristic, context->om);

			default:
				return BLE_ATT_ERR_UNLIKELY;
		}
	}


	static int HandleRead(ConnectionHandle connection, Characteristic *characteristic, struct os_mbuf *data) {
		if (characteristic->onRead != nullptr) {
			characteristic->onRead(connection);
		}

		if (os_mbuf_append(data, characteristic->GetValue(), characteristic->GetLength()) != 0) {
			return BLE_ATT_ERR_INSUFFICIENT_RES;
		}

		return 0;
	}


	static int HandleWrite(ConnectionHandle connection, Characteristic *characteristic, struct os_mbuf *data) {
		if (OS_MBUF_PKTLEN(data) > characteristic->GetCapacity()) {
			return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
		}

		uint16 length = 0;
		if (ble_hs_mbuf_to_flat(data, characteristic->GetStorage(), characteristic->GetCapacity(), &length) != 0) {
			return BLE_ATT_ERR_UNLIKELY;
		}

		characteristic->SetLength(length);
		if (characteristic->onWrite != nullptr) {
			characteristic->onWrite(connection);
		}

		return 0;
	}


	// Called in the NimBLE host task for every GAP event of the advertising, the scan and their connections.
	static int OnGapEvent(struct ble_gap_event *event, void *argument) {
		return static_cast<BLEAdapterESP *>(argument)->HandleGapEvent(event);
	}


	int HandleGapEvent(struct ble_gap_event *event) {
		switch (event->type) {
			case BLE_GAP_EVENT_CONNECT:
				if (event->connect.status == 0) {
					CallEvent(Event::Connected, event->connect.conn_handle);
				} else {
					CallEvent(Event::AdvertisingStopped, invalidConnection);
				}
				return 0;

			case BLE_GAP_EVENT_DISCONNECT:
				RemoveConnection(event->disconnect.conn.conn_handle);
				CallEvent(Event::Disconnected, event->disconnect.conn.conn_handle);
				return 0;

			case BLE_GAP_EVENT_ADV_COMPLETE:
				CallEvent(Event::AdvertisingStopped, invalidConnection);
				return 0;

			case BLE_GAP_EVENT_SUBSCRIBE:
				HandleSubscribe(event);
				return 0;

			case BLE_GAP_EVENT_MTU:
				CallEvent(Event::MtuChanged, event->mtu.conn_handle);
				return 0;

			case BLE_GAP_EVENT_ENC_CHANGE:
				CallEvent(Event::SecurityChanged, event->enc_change.conn_handle);
				return 0;

			case BLE_GAP_EVENT_PASSKEY_ACTION:
				HandlePasskey(event);
				return 0;

			case BLE_GAP_EVENT_REPEAT_PAIRING:
				return HandleRepeatPairing(event->repeat_pairing.conn_handle);

			case BLE_GAP_EVENT_DISC:
				ReportScanResult(event->disc);
				return 0;

			case BLE_GAP_EVENT_DISC_COMPLETE:
				scanCallback = nullptr;
				CallEvent(Event::ScanStopped, invalidConnection);
				return 0;

			default:
				return 0;
		}
	}


	void HandleSubscribe(struct ble_gap_event *event) {
		StoreSubscription(event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.cur_notify != 0, event->subscribe.cur_indicate != 0);

		Characteristic *characteristic = FindCharacteristic(event->subscribe.attr_handle);
		if (characteristic != nullptr && characteristic->onSubscribe != nullptr) {
			characteristic->onSubscribe(event->subscribe.conn_handle, event->subscribe.cur_notify != 0, event->subscribe.cur_indicate != 0);
		}
	}


	// Copies the entries of one value handle out of the table, so that the NimBLE calls run without the lock.
	uint8 CopySubscriptions(uint16 valueHandle, Subscription *targets) {
		uint8 count = 0;

		System::CriticalSection(true);
		for (uint8 i = 0; i < subscriptionCount; i++) {
			if (subscriptions[i].valueHandle == valueHandle) {
				targets[count] = subscriptions[i];
				count++;
			}
		}
		System::CriticalSection(false);

		return count;
	}


	// Builds a fresh mbuf for one peer. NimBLE consumes the mbuf on every path once it is handed over, so it is never
	// freed here after the call: notify at ble_gattc.c:5565-5573 (early exit at 5516-5519), indicate at
	// ble_gattc.c:6084-6089 and 6157-6166. A peer subscribed to both gets a notification, which has no
	// one-indication-at-a-time limit.
	static ResultStatus SendTo(const Subscription& target, const uint8 *value, uint16 length) {
		struct os_mbuf *om = ble_hs_mbuf_from_flat(value, length);
		if (om == nullptr) {
			return ResultStatus::outOfMemory;
		}

		int rc = 0;
		if (target.notify) {
			rc = ble_gatts_notify_custom(target.connection, target.valueHandle, om);
		} else {
			rc = ble_gatts_indicate_custom(target.connection, target.valueHandle, om);
		}

		return MapError(rc);
	}


	// Called in the NimBLE host task on every subscription change. With neither flag set, the entry is removed.
	void StoreSubscription(ConnectionHandle connection, uint16 valueHandle, bool notify, bool indicate) {
		System::CriticalSection(true);
		int index = FindSubscription(connection, valueHandle);
		if (!notify && !indicate) {
			if (index >= 0) {
				RemoveSubscription(static_cast<uint8>(index));
			}
		} else if (index >= 0) {
			subscriptions[index].notify = notify;
			subscriptions[index].indicate = indicate;
		} else if (subscriptionCount < maxSubscriptions) {
			subscriptions[subscriptionCount].connection = connection;
			subscriptions[subscriptionCount].valueHandle = valueHandle;
			subscriptions[subscriptionCount].notify = notify;
			subscriptions[subscriptionCount].indicate = indicate;
			subscriptionCount++;
		} else {
			refusedSubscriptions++;
		}
		System::CriticalSection(false);
	}


	// Called in the NimBLE host task when a connection ends. Its subscriptions go with it.
	void RemoveConnection(ConnectionHandle connection) {
		System::CriticalSection(true);
		uint8 index = 0;
		while (index < subscriptionCount) {
			if (subscriptions[index].connection == connection) {
				RemoveSubscription(index);
			} else {
				index++;
			}
		}
		System::CriticalSection(false);
	}


	// The caller holds the critical section. The last entry takes the place of the removed one.
	int FindSubscription(ConnectionHandle connection, uint16 valueHandle) {
		for (uint8 i = 0; i < subscriptionCount; i++) {
			if (subscriptions[i].connection == connection && subscriptions[i].valueHandle == valueHandle) {
				return i;
			}
		}

		return -1;
	}


	// The caller holds the critical section.
	void RemoveSubscription(uint8 index) {
		subscriptionCount--;
		subscriptions[index] = subscriptions[subscriptionCount];
	}


	// Only the display action is handled. Numeric comparison, keyboard entry and out-of-band data are not, so a pairing
	// that needs them is left waiting until the stack times out.
	void HandlePasskey(struct ble_gap_event *event) {
		if (event->passkey.params.action != BLE_SM_IOACT_DISP) {
			return;
		}

		ble_sm_io io = {};
		io.action = BLE_SM_IOACT_DISP;
		io.passkey = esp_random() % 1000000;
		if (ble_sm_inject_io(event->passkey.conn_handle, &io) == 0 && onPasskeyDisplay != nullptr) {
			onPasskeyDisplay(event->passkey.conn_handle, io.passkey);
		}
	}


	// The NimBLE example policy: a peer that pairs again replaces its old bond. That gives up the protection of the
	// old bond so that a peer which lost its keys can reconnect.
	static int HandleRepeatPairing(ConnectionHandle connection) {
		ble_gap_conn_desc desc = {};
		if (ble_gap_conn_find(connection, &desc) == 0) {
			ble_store_util_delete_peer(&desc.peer_id_addr);
		}

		return BLE_GAP_REPEAT_PAIRING_RETRY;
	}


	void ReportScanResult(const ble_gap_disc_desc& report) {
		if (scanCallback == nullptr) {
			return;
		}

		ScanResult result;
		result.address = ToAddress(report.addr);
		result.rssi = report.rssi;
		result.data = report.data;
		result.dataSize = report.length_data;
		result.connectable = (report.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND) || (report.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND);
		result.scanResponse = (report.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP);
		scanCallback(result);
	}


	Characteristic *FindCharacteristic(uint16 valueHandle) {
		for (Service *service = services; service != nullptr; service = service->next) {
			for (Characteristic *characteristic = service->characteristics; characteristic != nullptr; characteristic = characteristic->next) {
				if (characteristic->valueHandle == valueHandle) {
					return characteristic;
				}
			}
		}

		return nullptr;
	}


	// Called in the NimBLE host task after the host has synced with the controller.
	static void OnSync() {
		if (instance != nullptr) {
			instance->hostSynced = true;
		}
	}


	static void OnReset(int reason) {
		(void)reason;
		if (instance != nullptr) {
			instance->hostSynced = false;
		}
	}


	static void HostTask(void *argument) {
		(void)argument;
		nimble_port_run();
		nimble_port_freertos_deinit();
	}


	// NimBLE counts intervals and windows in units of 0.625 ms.
	static constexpr uint16 MsToUnits(uint32 ms) {
		return static_cast<uint16>(ms * 8 / 5);
	}


	// 0 means the operation runs until it is stopped.
	static int32 Duration(uint32 ms) {
		if (ms == 0 || ms > static_cast<uint32>(BLE_HS_FOREVER)) {
			return BLE_HS_FOREVER;
		}

		return static_cast<int32>(ms);
	}


	static ResultStatus StoreUuid(const Uuid& uuid, ble_uuid_any_t& out) {
		if (uuid.size == 2) {
			out.u16 = ToNimble16(uuid);
			return ResultStatus::ok;
		}

		if (uuid.size == Uuid::maxSize) {
			out.u128 = ToNimble128(uuid);
			return ResultStatus::ok;
		}

		return ResultStatus::invalidParameter;
	}


	static ble_uuid16_t ToNimble16(const Uuid& uuid) {
		ble_uuid16_t result = {};
		result.u.type = BLE_UUID_TYPE_16;
		result.value = static_cast<uint16>((uuid.bytes[0] << 8) | uuid.bytes[1]);
		return result;
	}


	// NimBLE keeps the 128-bit value little-endian, the reverse of the written order.
	static ble_uuid128_t ToNimble128(const Uuid& uuid) {
		ble_uuid128_t result = {};
		result.u.type = BLE_UUID_TYPE_128;
		for (uint8 i = 0; i < Uuid::maxSize; i++) {
			result.value[i] = uuid.bytes[Uuid::maxSize - 1 - i];
		}

		return result;
	}


	// NimBLE stores addresses little-endian. MacAddress is in the order printed.
	static MacAddress ToMacAddress(const uint8 *littleEndian) {
		MacAddress result;
		for (uint8 i = 0; i < MacAddress::size; i++) {
			result.bytes[i] = littleEndian[MacAddress::size - 1 - i];
		}

		return result;
	}


	static Address ToAddress(const ble_addr_t& addr) {
		Address result;
		result.address = ToMacAddress(addr.val);
		bool random = (addr.type == BLE_ADDR_RANDOM) || (addr.type == BLE_ADDR_RANDOM_ID);
		result.type = random ? AddressType::Random : AddressType::Public;
		return result;
	}


	static ble_gatt_chr_flags CastFlags(const Characteristic& characteristic) {
		ble_gatt_chr_flags flags = 0;

		if (characteristic.properties.read) {
			flags |= BLE_GATT_CHR_F_READ;
			flags |= SecurityFlags(characteristic.readSecurity, BLE_GATT_CHR_F_READ_ENC, BLE_GATT_CHR_F_READ_AUTHEN);
		}

		if (characteristic.properties.write) {
			flags |= BLE_GATT_CHR_F_WRITE;
			flags |= SecurityFlags(characteristic.writeSecurity, BLE_GATT_CHR_F_WRITE_ENC, BLE_GATT_CHR_F_WRITE_AUTHEN);
		}

		if (characteristic.properties.writeWithoutResponse) {
			flags |= BLE_GATT_CHR_F_WRITE_NO_RSP;
			flags |= SecurityFlags(characteristic.writeSecurity, BLE_GATT_CHR_F_WRITE_ENC, BLE_GATT_CHR_F_WRITE_AUTHEN);
		}

		if (characteristic.properties.notify) {
			flags |= BLE_GATT_CHR_F_NOTIFY;
		}

		if (characteristic.properties.indicate) {
			flags |= BLE_GATT_CHR_F_INDICATE;
		}

		return flags;
	}


	static ble_gatt_chr_flags SecurityFlags(Security security, ble_gatt_chr_flags encrypted, ble_gatt_chr_flags authenticated) {
		switch (security) {
			case Security::Encrypted:
				return encrypted;

			case Security::Authenticated:
				return authenticated;

			default:
				return 0;
		}
	}


	static uint8 CastIoCapability(IoCapability capability) {
		switch (capability) {
			case IoCapability::DisplayOnly:
				return BLE_SM_IO_CAP_DISP_ONLY;

			case IoCapability::KeyboardOnly:
				return BLE_SM_IO_CAP_KEYBOARD_ONLY;

			case IoCapability::DisplayYesNo:
				return BLE_SM_IO_CAP_DISP_YES_NO;

			case IoCapability::KeyboardDisplay:
				return BLE_SM_IO_CAP_KEYBOARD_DISP;

			case IoCapability::None:
			default:
				return BLE_SM_IO_CAP_NO_IO;
		}
	}


	static ResultStatus MapError(int rc) {
		switch (rc) {
			case 0:
				return ResultStatus::ok;

			case BLE_HS_EALREADY:
			case BLE_HS_EBUSY:
				return ResultStatus::busy;

			case BLE_HS_ENOTCONN:
				return ResultStatus::noConnection;

			case BLE_HS_EINVAL:
			case BLE_HS_EMSGSIZE:
				return ResultStatus::invalidParameter;

			case BLE_HS_ENOMEM:
				return ResultStatus::outOfMemory;

			default:
				return ResultStatus::error;
		}
	}
};
