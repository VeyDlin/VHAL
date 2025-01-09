#pragma once


class IRegisterMap;

class IRegisterData {
protected:
    IRegisterMap* map;

public:
    void LinkToMap(IRegisterMap& mapRef) {
        map = &mapRef;
    }
};