#pragma once
#include "Allocator.h"


#define SmartPointerDebug					printf


template <typename DataType>
class SmartPointer {
private:
    DataType* pointer = nullptr;
    Allocator* allocator;

public:
    bool replace_flag = false;



public:
    SmartPointer() {}



    SmartPointer(DataType& data, Allocator& _allocator) {
        SetAllocator(&_allocator);
        SetDataPointer(data);
        replace_flag = false;
    }



    SmartPointer(const SmartPointer& smartPointer) {
        SetAllocator(smartPointer.GetAllocator());
        if (allocator->ReplacePointer(smartPointer.GetPointer(), pointer)) {
            smartPointer.replace_flag = true;
        }
    }



    ~SmartPointer() {
        if (pointer != nullptr && replace_flag == false) {
            allocator->Free(pointer);
        }
    }





    bool Allocate(uint32_t elementsCount = 1) {
        if (pointer != nullptr) {
            return false;
        }
        allocator->Malloc(elementsCount * sizeof(DataType), pointer);
        return pointer != nullptr;
    }



    bool Free() {
        if (pointer == nullptr) {
            return false;
        }
        allocator->Free(pointer);
        return true;
    }



    bool Replace() {

    }



    Allocator* GetAllocator() const {
        return allocator;
    }



    void SetAllocator(Allocator* _allocator) {
        allocator = _allocator;
    }



    DataType* GetPointer() {
        return pointer;
    }



    bool SetDataPointer(DataType& data) {
        return allocator->ReplacePointer(&data, pointer);
    }





    SmartPointer<DataType>& operator = (const SmartPointer& smartPointer) {
        if (allocator->ReplacePointer(smartPointer.GetPointer(), pointer)) {
            smartPointer.replace_flag = true;
        }

        return *this;
    };



    SmartPointer<DataType>& operator = (DataType& data) {
        allocator->ReplacePointer(&data, pointer);
        return *this;
    };



    DataType& operator*() {
        return *pointer;
    }



    DataType& operator[](uint32_t i) {
        return pointer[i];
    }
};