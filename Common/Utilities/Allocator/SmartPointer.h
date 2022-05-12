#pragma once
#include "Allocator.h"


#define SmartPointerDebug					printf


template <typename DataType>
class SmartPointer {
private:
	DataType* pointer = nullptr;
    Allocator *allocator;

public:
    bool replace_flag = false;



public:
    SmartPointer(){ }

    

    SmartPointer(DataType* &ptr){
        SetAllocator(&default_heap);
        if(!assignPtr(ptr){
            SmartPointerDebug(" can't assign pointer");
        }
        replace_flag = false;
    }



    SmartPointer(const SmartPointer &sm_ptr_obj){
        SetAllocator(sm_ptr_obj.GetAllocator());

        if(allocator->ValidatePointer((void**)&(sm_ptr_obj.pointer))){
            allocator->ReplacePointer((void**)&(sm_ptr_obj.pointer), (void**)&(pointer));
            sm_ptr_obj.replace_flag = true;
        }
        else {
            SmartPointerDebug(" can't replace pointers");
        }
    }



    ~SmartPointer(){
        if((pointer != NULL) && (replace_flag == false)){
            allocator->Free((void**)&pointer);
        }
    }





    DataType* get(){
        return pointer;
    }





    bool assignPtr(DataType* &ptr){
        if(allocator->ValidatePointer((void**)&ptr)){
            allocator->ReplacePointer((void**)&ptr, (void**)&pointer);
            return true;
        }
        return false;
    }

    



    bool allocate(uint32_t elements_num){
        if(pointer != NULL){
            return false;
        }

        dalloc(elements_num*sizeof(DataType), (void**)&pointer);
        if(pointer == NULL){
            return false;
        }
        return true;
    }





    bool free(){
        if((pointer != NULL)){
            allocator->Free((void**)&pointer);
            return true;
        }
        return false;
    }





    SmartPointer<DataType>& operator = (const SmartPointer &sm_ptr_obj){
        if(&sm_ptr_obj != this) {
            if(allocator->ValidatePointer((void**)&(sm_ptr_obj.pointer))){
                allocator->ReplacePointer((void**)&(sm_ptr_obj.pointer), (void**)&(pointer));
                sm_ptr_obj.replace_flag = true;
            } else {
                SmartPointerDebug(" can't replace pointers");
            }
        }
        return *this;
    };

    



    SmartPointer<DataType>& operator = (DataType* &ptr){
        if(&ptr != this){
            if(allocator->ValidatePointer((void**)&ptr)){
                allocator->ReplacePointer((void**)&ptr, (void**)&(pointer));
            }
            else {
                SmartPointerDebug(" can't replace pointers");
            }
        }
        return *this;
    };





    DataType& operator*(){
        return *pointer;
    }

    

    DataType& operator[](uint32_t i){
        return pointer[i];
    }

    

    Allocator* GetAllocator() const{
        return allocator;
    }



    void SetAllocator(Allocator* _allocator){
        allocator = allocator;
    }
};