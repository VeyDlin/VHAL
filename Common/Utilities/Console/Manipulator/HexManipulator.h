#pragma once
#include <System/System.h>


class HexManipulator {
private:
    uint8* data;
    size_t size;
public:
    HexManipulator(uint8* data, size_t size) : data(data), size(size) {}
    
    uint8* getData() const { return data; }
    size_t getSize() const { return size; }
};
