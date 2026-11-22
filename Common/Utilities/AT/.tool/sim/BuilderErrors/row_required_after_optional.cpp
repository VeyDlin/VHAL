// expect: a required field comes after an optional one
#include <Utilities/AT/AtRow.h>

#include <optional>

struct Reply {
	std::optional<int8> channel{};
	int8 value = 0;
};

constexpr auto ReplyRow = AtRow<Reply>(AtField<&Reply::channel>("channel"), AtField<&Reply::value>("value"));

static_assert(ReplyRow.FieldCount == 2);
