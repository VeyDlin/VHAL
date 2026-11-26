#pragma once


namespace VUB {


// Holds a value as a template argument. A requires clause names it with a member of a block: the clause fails when the member is not
// a constant expression, because the builder reads the members of a block at compile time.
template<auto Value>
struct Constant {
};

}
