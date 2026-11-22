// expect: a Close text is empty
#include <Utilities/AT/AtRow.h>

struct Reply {
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::value>("value")).Close("");

static_assert(ReplyRow.FieldCount == 1);
