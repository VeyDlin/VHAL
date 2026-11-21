#include <Utilities/VSTD/VSTD.h>
#include "SimCheck.h"

namespace {

// Front and Back of a container that can be empty return a pointer, which is null when the container is empty.
void VectorAccessorsOnEmptyReturnNull() {
    static uint8_t buffer[256];
    VSTD::Heap<> heap(buffer, sizeof(buffer));

    VSTD::Vector<int> empty(&heap);
    SIM_CHECK(empty.Front() == nullptr);
    SIM_CHECK(empty.Back() == nullptr);

    const VSTD::Vector<int>& constEmpty = empty;
    SIM_CHECK(constEmpty.Front() == nullptr);
    SIM_CHECK(constEmpty.Back() == nullptr);

    VSTD::Vector<int> filled(&heap);
    SIM_CHECK(filled.PushBack(1));
    SIM_CHECK(filled.PushBack(2));
    SIM_CHECK(*filled.Front() == 1);
    SIM_CHECK(*filled.Back() == 2);
}

void SpanAccessorsOnEmptyReturnNull() {
    VSTD::Span<int> empty;
    SIM_CHECK(empty.Front() == nullptr);
    SIM_CHECK(empty.Back() == nullptr);

    int raw[3] = {1, 2, 3};
    VSTD::Span<int> filled(raw);
    SIM_CHECK(*filled.Front() == 1);
    SIM_CHECK(*filled.Back() == 3);

    VSTD::Span<int> zeroLength(raw, 0);
    SIM_CHECK(zeroLength.Front() == nullptr);
    SIM_CHECK(zeroLength.Back() == nullptr);
}

void StringViewAccessorsOnEmptyReturnNull() {
    VSTD::StringView empty;
    SIM_CHECK(empty.Front() == nullptr);
    SIM_CHECK(empty.Back() == nullptr);

    VSTD::StringView zeroLength("abc", 0);
    SIM_CHECK(zeroLength.Front() == nullptr);
    SIM_CHECK(zeroLength.Back() == nullptr);

    VSTD::StringView filled("abc");
    SIM_CHECK(*filled.Front() == 'a');
    SIM_CHECK(*filled.Back() == 'c');
}

}

int main() {
    VectorAccessorsOnEmptyReturnNull();
    SpanAccessorsOnEmptyReturnNull();
    StringViewAccessorsOnEmptyReturnNull();
    return VstdSim::Finish("TestAccessors");
}
