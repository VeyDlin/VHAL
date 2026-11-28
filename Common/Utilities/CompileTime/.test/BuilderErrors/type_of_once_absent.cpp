// expect: BlockMissing
// expect-value: 0
#include <Utilities/CompileTime/TypeList.h>
#include <tuple>


struct BlockMissing {
};


struct Present {
};


constexpr std::size_t index = TypeList::IndexOfOnce<BlockMissing, std::tuple<Present>>();
