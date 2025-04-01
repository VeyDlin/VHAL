#pragma once
#include <System/System.h>

class IRegisterMap;

class IRegisterData {
    friend class IRegisterMap;
protected:
    IRegisterMap* map;

public:
    bool write = true;

    void LinkToMap(IRegisterMap& mapRef) {
        map = &mapRef;
    }
    virtual size_t DataTypeSize() = 0;
    virtual uint32 Addres() = 0;
    virtual bool WriteEvent(const uint8* buffer, size_t length) = 0;
};