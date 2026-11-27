// expect: two fields of a row have the same name
#include <Utilities/AT/AtRow.h>

struct Reply {
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::value>("value"), AtField<&Reply::value>("value"));

static_assert(ReplyRow.FieldCount == 2);
