#include <Utilities/VSTD/VSTD.h>
#include "SimCheck.h"

namespace {

// With no default heap, every growing operation of a Vector must refuse and leave it empty. Nothing may crash.
void VectorRefusesWithoutHeap() {
    VSTD::SetDefaultHeap(nullptr);

    VSTD::Vector<int> v;
    SIM_CHECK(!v.PushBack(1));
    SIM_CHECK(v.EmplaceBack(2) == nullptr);
    SIM_CHECK(!v.Reserve(4));
    SIM_CHECK(!v.Resize(3));
    SIM_CHECK(!v.Insert(0, 5));
    SIM_CHECK(v.Size() == 0);
    SIM_CHECK(v.Capacity() == 0);

    VSTD::Vector<int> copy(v);
    SIM_CHECK(copy.Empty());
    SIM_CHECK(copy.Assign(v));

    VSTD::Vector<int> assigned;
    assigned = v;
    SIM_CHECK(assigned.Empty());
}

// A String with no default heap is empty. Its text is still terminated, and every growing call returns false.
void StringRefusesWithoutHeap() {
    VSTD::SetDefaultHeap(nullptr);

    VSTD::String text("hello");
    SIM_CHECK(text.Empty());
    SIM_CHECK(!text.Append(" world"));
    SIM_CHECK(!text.Append('!'));
    SIM_CHECK(!text.Insert(0, "x"));
    SIM_CHECK(!text.Insert(0, 'x'));
    SIM_CHECK(text.Data()[0] == '\0');

    VSTD::String sum = text + "tail";
    SIM_CHECK(sum.Empty());

    VSTD::String copy(text);
    SIM_CHECK(copy.Empty());
}

}

int main() {
    VectorRefusesWithoutHeap();
    StringRefusesWithoutHeap();
    return VstdSim::Finish("TestNoDefaultHeap");
}
