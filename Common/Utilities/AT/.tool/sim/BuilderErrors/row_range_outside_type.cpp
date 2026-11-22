// expect: a range bound is outside the type of the field
#include <Utilities/AT/AtRow.h>

struct Reply {
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::value>("value").Range(-200, 0));

static_assert(ReplyRow.FieldCount == 1);
