// expect: a range is for an integer field
#include <Utilities/AT/AtRow.h>

struct Reply {
	String<8> name{};
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::name>("name").Range(0, 4));

static_assert(ReplyRow.FieldCount == 1);
