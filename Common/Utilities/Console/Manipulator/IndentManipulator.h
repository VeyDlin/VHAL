#pragma once
#include <System/System.h>


class IndentManipulator {
private:
    size_t spaces;
public:
    IndentManipulator(size_t spaces) : spaces(spaces) {}
    
    size_t getSpaces() const { return spaces; }
};
