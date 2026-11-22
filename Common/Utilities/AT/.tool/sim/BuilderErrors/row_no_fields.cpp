// expect: a row has no fields
#include <Utilities/AT/AtRow.h>

struct Reply {
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>();

static_assert(ReplyRow.FieldCount == 0);
