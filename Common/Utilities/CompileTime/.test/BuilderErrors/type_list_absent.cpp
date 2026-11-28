// expect: BlockMissing
// expect-value: 0
#include <Utilities/CompileTime/TypeList.h>
#include <tuple>
#include <type_traits>


template<typename Block>
struct HasBlock {
	template<typename Element>
	static constexpr bool Holds() {
		return std::is_same_v<typename Element::BlockType, Block>;
	}
};


struct BlockMissing {
};


struct Present {
	using BlockType = int;
};


constexpr std::size_t index = TypeList::IndexWhereOnce<std::tuple<Present>, HasBlock<BlockMissing>>();
