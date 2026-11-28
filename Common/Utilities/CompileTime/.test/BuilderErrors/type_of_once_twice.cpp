// expect: SharedType
// expect-value: 2
#include <Utilities/CompileTime/TypeList.h>
#include <tuple>


struct SharedType {
};


constexpr std::size_t index = TypeList::IndexOfOnce<SharedType, std::tuple<SharedType, SharedType>>();
