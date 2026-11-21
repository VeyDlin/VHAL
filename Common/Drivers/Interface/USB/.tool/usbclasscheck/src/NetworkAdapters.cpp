#include "NetworkAdapters.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// netioapi.h comes before iphlpapi.h: included the other way round, it leaves out the neighbour table calls.
#include <winsock2.h>
#include <netioapi.h>
#include <iphlpapi.h>
#include <windows.h>

#include "NetworkLogic.h"
#include "WinText.h"

namespace {

// The adapter table is read into a buffer of this size. When Windows needs more, the call says how much and is made again.
constexpr ULONG InitialTableBytes = 16 * 1024;
constexpr int TableAttempts = 3;


// The IPv4 address of a socket address, in host order. False for another family.
bool Ipv4Of(const SOCKET_ADDRESS& socketAddress, std::uint32_t& address) {
	if (socketAddress.lpSockaddr == nullptr || socketAddress.lpSockaddr->sa_family != AF_INET) {
		return false;
	}

	const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(socketAddress.lpSockaddr);
	address = ntohl(ipv4->sin_addr.S_un.S_addr);
	return true;
}


AdapterInfo AdapterOf(const IP_ADAPTER_ADDRESSES* node) {
	AdapterInfo adapter;
	adapter.interfaceIndex = node->IfIndex;
	adapter.name = NarrowUtf8(std::wstring(node->FriendlyName));
	adapter.description = NarrowUtf8(std::wstring(node->Description));
	for (ULONG index = 0; index < node->PhysicalAddressLength && index < sizeof(node->PhysicalAddress); index++) {
		adapter.mac.push_back(node->PhysicalAddress[index]);
	}

	adapter.up = node->OperStatus == IfOperStatusUp;
	adapter.transmitBitsPerSecond = node->TransmitLinkSpeed;
	adapter.receiveBitsPerSecond = node->ReceiveLinkSpeed;
	for (const IP_ADAPTER_UNICAST_ADDRESS* unicast = node->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
		AdapterAddress entry;
		if (!Ipv4Of(unicast->Address, entry.address)) {
			continue;
		}

		entry.prefixLength = unicast->OnLinkPrefixLength;
		entry.preferred = unicast->DadState == IpDadStatePreferred;
		adapter.addresses.push_back(entry);
	}

	for (const IP_ADAPTER_GATEWAY_ADDRESS* gateway = node->FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next) {
		std::uint32_t address = 0;
		if (Ipv4Of(gateway->Address, address)) {
			adapter.gateways.push_back(address);
		}
	}

	return adapter;
}

}


bool EnumerateAdapters(std::vector<AdapterInfo>& adapters, std::string& error) {
	ULONG size = InitialTableBytes;
	std::vector<std::uint8_t> buffer;
	ULONG status = ERROR_BUFFER_OVERFLOW;
	for (int attempt = 0; attempt < TableAttempts && status == ERROR_BUFFER_OVERFLOW; attempt++) {
		buffer.resize(size);
		// AF_UNSPEC, not AF_INET: an adapter with no IPv4 address yet is still listed, so the wait for the lease can see it.
		// Ipv4Of() keeps only the IPv4 addresses of each adapter.
		status = GetAdaptersAddresses(
			AF_UNSPEC,
			GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
			nullptr,
			reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
			&size);
	}

	if (status != NO_ERROR) {
		error = "GetAdaptersAddresses failed: " + FormatWindowsError(status);
		return false;
	}

	adapters.clear();
	const IP_ADAPTER_ADDRESSES* node = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
	for (; node != nullptr; node = node->Next) {
		adapters.push_back(AdapterOf(node));
	}

	return true;
}


bool EnumerateArpEntries(std::vector<ArpEntry>& entries, std::string& error) {
	PMIB_IPNET_TABLE2 table = nullptr;
	NETIO_STATUS status = GetIpNetTable2(AF_INET, &table);
	if (status != NO_ERROR) {
		error = "GetIpNetTable2 failed: " + FormatWindowsError(status);
		return false;
	}

	entries.clear();
	for (ULONG index = 0; index < table->NumEntries; index++) {
		const MIB_IPNET_ROW2& row = table->Table[index];
		if (row.Address.si_family != AF_INET) {
			continue;
		}

		ArpEntry entry;
		entry.interfaceIndex = row.InterfaceIndex;
		entry.address = ntohl(row.Address.Ipv4.sin_addr.S_un.S_addr);
		entry.mac.assign(row.PhysicalAddress, row.PhysicalAddress + row.PhysicalAddressLength);
		entries.push_back(entry);
	}

	FreeMibTable(table);
	return true;
}
