// expect: RawString is the last field of a row
#include <Utilities/AT/AtRow.h>

struct Reply {
	RawString<8> text{};
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::text>("text"), AtField<&Reply::value>("value"));

static_assert(ReplyRow.FieldCount == 2);
