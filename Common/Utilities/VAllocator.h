#pragma once
#include "stdint.h"
#include "stdbool.h"
#include "stdio.h"
#include "string.h"


#define VAllocatorDebug printf


class VAllocator {
private:
    static constexpr size_t heapSize = 64 * 1024;
    static constexpr size_t allocatorsSize = 100;

    struct Pointer {
        uint8_t **memory = nullptr;
        uint32_t size = 0;
        bool free = true;
    };

    struct Allocators {
        Pointer pointer[allocatorsSize];
        uint32_t count = 0;
    };

    struct Heap {
        uint8_t *memory = nullptr;		// Pointer to the memory area using for heap
        uint32_t offset = 0;		    // Size of currently allocated memory
        uint32_t size = 0;			    // Total size of memory that can be used for allocation memory
        Allocators allocators;
    } heap;

    Heap heap;
    uint8_t heapArray[heapSize] = {0};

    uint8_t alignment = 4;
    bool fillFreeMemory = true;



public:
    VAllocator(){
        heap.memory = heapArray;
        heap.size = heapSize;
    }




    void Malloc(uint32_t size, void **pointer){
        if(!size){
            *pointer = nullptr;
        }

        uint32_t offset = heap.offset + size;

        // Correct offset if use alignment
        if(alignment) {
            while(offset % alignment != 0){
                offset += 1;
            }
        }

        // Check if there is enough memory for new allocation, and if number of allocations is exceeded
        if((offset <= heap.size) && (heap.allocators.count < allocatorsSize)) {
            *pointer = heap.memory + heap.offset;
            heap.offset = offset;

            // Save info about allocated memory
            heap.allocators.pointer[heap.allocators.count].memory = (uint8_t**)pointer;
            heap.allocators.pointer[heap.allocators.count].size = size;
            heap.allocators.pointer[heap.allocators.count].free = false;
            heap.allocators.count++;
        } else {
            *pointer = nullptr;
            if(offset > heap.size){
                VAllocatorDebug("Heap size exceeded");
            }
            if(heap.allocators.count > allocatorsSize){
                VAllocatorDebug("Max number of allocations exceeded");
            }
        }
    }





    void Free(void **pointer){
        uint32_t index = 0;

        // Try to find given ptr in ptr_info array
        if(!ValidatePointer(pointer, index)){
            VAllocatorDebug("Try to free unexisting pointer");
            return;
        }

        uint32_t size = heap.allocators.pointer[index].size;
        if(alignment) {
            while(size % alignment != 0){
                size += 1;
            }
        }

        // Edit pointer info array
        heap.allocators.pointer[index].free = true;
        if(fillFreeMemory) {
            for(uint32_t i = 0; i < size; i++){
                *(*(heap.allocators.pointer[index].memory) + i) = 0;
            }
        }

        Defragmentation();
    }





    bool Realloc(uint32_t size, void **pointer){
        uint32_t sizeOld = 0;
        uint32_t indexOld = 0;

        if(ValidatePointer(pointer, indexOld) == true){
            sizeOld = heap.allocators.pointer[indexOld].size;
        } else {
            return false;
        }

        uint8_t *newPointer = nullptr;
        Malloc(size, (void**)&newPointer);

        uint8_t *old_ptr = (uint8_t *)(*pointer);

        for(uint32_t i = 0; i < sizeOld; i++){
            newPointer[i] = old_ptr[i];
        }

        Free(pointer);
        ReplacePointer((void**)&newPointer, pointer);

        return true;
    }



private:
    void Defragmentation(){
        for(uint32_t i = 0; i < heap.allocators.count; i++){
            if(heap.allocators.pointer[i].free == true){

                // Optimize memory
                uint8_t *memoryStart = *(heap.allocators.pointer[i].memory);
                uint32_t indexStart = (uint32_t)(memoryStart - heap.memory);

                // Set given ptr to nullptr
                *(heap.allocators.pointer[i].memory) = nullptr;

                uint32_t allocateSize = heap.allocators.pointer[i].size;
                if(alignment) {
                    while(allocateSize % alignment != 0){
                        allocateSize += 1;
                    }
                }

                // Check if ptrs adresses of defragmentated memory are in heap region
                for(uint32_t k = i + 1; k < heap.allocators.count; k++){
                    if(IsPointerInHeapArea((void**)heap.allocators.pointer[k].memory)){
                        if((size_t)heap.allocators.pointer[k].memory > (size_t)(memoryStart)){
                            heap.allocators.pointer[k].memory = (uint8_t**)((size_t)(heap.allocators.pointer[k].memory) - allocateSize);
                        }
                    }
                }

                // Defragmentate memory
                uint32_t indexEnd = heap.offset - allocateSize;
                for(uint32_t k = indexStart; k <= indexEnd; k++){
                    *(heap.memory + k) = *(heap.memory + k + allocateSize);
                }

                // Reassign pointers
                for(uint32_t k = i + 1; k < heap.allocators.count; k++){
                    *(heap.allocators.pointer[k].memory) -= allocateSize;
                }

                // Actualize ptr info array
                for(uint32_t k = i; k < heap.allocators.count - 1; k++){
                    heap.allocators.pointer[k] = heap.allocators.pointer[k + 1];
                }

                // Decrement allocations number
                heap.allocators.count--;

                // Refresh offset
                heap.offset = heap.offset - allocateSize;

                // Fill by 0 all freed memory
                if(fillFreeMemory){
                    for(uint32_t k = 0; k < allocateSize; k++){
                        heap.memory[heap.offset + k] = 0;
                    }
                }
            }
        }
    }





    bool IsPointerInHeapArea(void **pointer){
        size_t heap_start_area = (size_t)(heap.memory);
        size_t heap_stop_area = (size_t)(heap.memory) + heap.size;
        if(((size_t)pointer >= heap_start_area) && ((size_t)pointer <= heap_stop_area)){
            return true;
        }
        return false;
    }





    bool ValidatePointer(void **pointer, uint32_t &index){
        for(uint32_t i = 0; i < heap.allocators.count; i++){
            if(heap.allocators.pointer[i].memory == (uint8_t**)pointer){
                index = i;
                return true;
            }
        }
        return false;
    }





    void ReplacePointer(void **replacePointer, void **newPointer){
        uint32_t index = 0;
        if(!ValidatePointer(replacePointer, index)){
            VAllocatorDebug("Can't replace pointers. No pointer found in buffer");
            return;
        }

        *newPointer = *replacePointer;
        heap.allocators.pointer[index].memory = (uint8_t**)newPointer;
        *replacePointer = nullptr;
    }
};





