// expect: SharedBlock
// expect-value: 2
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


struct SharedBlock {
};


struct First {
	using BlockType = SharedBlock;
};


struct Second {
	using BlockType = SharedBlock;
};


constexpr std::size_t index = TypeList::IndexWhereOnce<std::tuple<First, Second>, HasBlock<SharedBlock>>();
