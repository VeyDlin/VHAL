#pragma once
#include <VHAL.h>

class IRegisterMap;

class IRegisterData {
    friend class IRegisterMap;
protected:
    IRegisterMap* map;

public:
    bool write = true;
    bool read = true;

    void LinkToMap(IRegisterMap& mapRef) {
        map = &mapRef;
    }
	virtual size_t DataTypeSize() const = 0;
	virtual uint32 Addres() const = 0;

	virtual bool WriteEvent(const uint8* buffer) = 0;

	virtual bool CanOnWrite() const = 0;
	virtual bool CanOnRead() const = 0;
	virtual size_t GetUnsafe(uint8* outData) const = 0;
};
