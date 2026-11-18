#pragma once
#include <System/System.h>


// Fixed-size address types shared by the wireless and network adapters. They hold no heap memory, so they can be
// passed by value from an event callback.


struct MacAddress {
	static constexpr uint8 size = 6;

	uint8 bytes[size] = {};

	constexpr bool operator==(const MacAddress& other) const noexcept {
		for (uint8 i = 0; i < size; i++) {
			if (bytes[i] != other.bytes[i]) {
				return false;
			}
		}
		return true;
	}

	constexpr bool operator!=(const MacAddress& other) const noexcept {
		return !(*this == other);
	}

	constexpr bool IsZero() const noexcept {
		for (uint8 i = 0; i < size; i++) {
			if (bytes[i] != 0) {
				return false;
			}
		}
		return true;
	}
};



// bytes[0] is the first octet as written: 192.168.1.10 is { 192, 168, 1, 10 }.
struct Ipv4Address {
	static constexpr uint8 size = 4;

	uint8 bytes[size] = {};

	constexpr Ipv4Address() noexcept = default;

	constexpr Ipv4Address(uint8 first, uint8 second, uint8 third, uint8 fourth) noexcept
		: bytes{ first, second, third, fourth } { }

	constexpr bool operator==(const Ipv4Address& other) const noexcept {
		for (uint8 i = 0; i < size; i++) {
			if (bytes[i] != other.bytes[i]) {
				return false;
			}
		}
		return true;
	}

	constexpr bool operator!=(const Ipv4Address& other) const noexcept {
		return !(*this == other);
	}

	constexpr bool IsZero() const noexcept {
		return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
	}
};
