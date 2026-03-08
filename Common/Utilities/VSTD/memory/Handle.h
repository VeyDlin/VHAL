#pragma once

#include <cstddef>

namespace VSTD {

struct Entry {
    void*       data = nullptr;
    std::size_t size = 0;
    bool        alive = false;
};

class Handle {
private:
    Entry* entry_;

public:
    Handle() : entry_(nullptr) {}
    explicit Handle(Entry* e) : entry_(e) {}


    bool IsValid() const {
        return entry_ != nullptr && entry_->alive;
    }

    void* Data() const {
        if (!IsValid()) return nullptr;
        return entry_->data;
    }

    std::size_t Size() const {
        if (!IsValid()) return 0;
        return entry_->size;
    }

    void Invalidate() {
        if (entry_) {
            entry_->alive = false;
            entry_->data = nullptr;
            entry_->size = 0;
        }
        entry_ = nullptr;
    }

    Entry* GetEntry() const { return entry_; }
};

}
