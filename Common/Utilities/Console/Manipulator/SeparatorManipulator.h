#pragma once
#include <System/System.h>


class SeparatorManipulator {
public:
	static constexpr char* defaultSeparator = "----------------------------------------";

private:
    const char* separator;

public:
    SeparatorManipulator() : separator(defaultSeparator) { }
    SeparatorManipulator(const char* sep) : separator(sep == nullptr ? defaultSeparator : sep) { }
    
    const char* getSeparator() const { return separator; }
};
