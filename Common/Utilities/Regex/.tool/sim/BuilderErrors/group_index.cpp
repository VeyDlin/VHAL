// expect: group index is above the group count
#include <Utilities/Regex/RegexError.h>

// Group<I>() calls this check with I and the group count of the pattern.
constexpr bool Checked = (RegexRequireGroup<3, 2>(), true);
