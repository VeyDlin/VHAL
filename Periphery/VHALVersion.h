#pragma once
#include <System/System.h>


struct VHALVersion {
	uint8 major = 0;
	uint8 minor = 0;
	uint16 patch = 0;

	static constexpr VHALVersion Current() {
		return { 0, 1, 1 };
	}

	static constexpr const char* String() {
		return "0.1.1";
	}

	constexpr uint32 UInt32() const {
		return (static_cast<uint32>(major) << 24) | (static_cast<uint32>(minor) << 16) | patch;
	}

	constexpr bool operator==(const VHALVersion& o) const { return UInt32() == o.UInt32(); }
	constexpr bool operator!=(const VHALVersion& o) const { return UInt32() != o.UInt32(); }
	constexpr bool operator< (const VHALVersion& o) const { return UInt32() <  o.UInt32(); }
	constexpr bool operator<=(const VHALVersion& o) const { return UInt32() <= o.UInt32(); }
	constexpr bool operator> (const VHALVersion& o) const { return UInt32() >  o.UInt32(); }
	constexpr bool operator>=(const VHALVersion& o) const { return UInt32() >= o.UInt32(); }
};