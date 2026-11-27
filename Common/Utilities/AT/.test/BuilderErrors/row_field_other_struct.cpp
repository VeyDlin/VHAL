// expect: a row field is a member of another struct
#include <Utilities/AT/AtRow.h>

struct Reply {
	int8 value = 0;
};

struct Other {
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Other::value>("value"));

static_assert(ReplyRow.FieldCount == 1);
