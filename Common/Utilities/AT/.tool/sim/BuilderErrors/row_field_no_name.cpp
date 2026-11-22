// expect: a row field has no name
#include <Utilities/AT/AtRow.h>

struct Reply {
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::value>(""));

static_assert(ReplyRow.FieldCount == 1);
