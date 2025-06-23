#pragma once
#include <System/System.h>


template<typename T>
class NumberManipulator {
private:
    T number;
    Print::Format format;
public:
    NumberManipulator(T number, Print::Format format) : number(number), format(format) {}
    
    T getNumber() const { return number; }
    Print::Format getFormat() const { return format; }
};
