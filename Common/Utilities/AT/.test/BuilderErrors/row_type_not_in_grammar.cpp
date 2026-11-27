// expect: a row field has a type the grammar does not carry
#include <Utilities/AT/AtRow.h>

struct Reply {
	float level = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::level>("level"));

static_assert(ReplyRow.FieldCount == 1);
