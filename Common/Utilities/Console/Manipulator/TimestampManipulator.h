#pragma once
#include <System/System.h>


class TimestampManipulator {
public:
    enum class Format {
        HMS,        // HH:MM:SS
        HMSM,       // HH:MM:SS:MS
        Auto        // Automatically add days/months/years if needed
    };
    
private:
    Format format;
    
public:
    TimestampManipulator(Format fmt = Format::Auto) : format(fmt) {}
    
    Format getFormat() const { return format; }
};
