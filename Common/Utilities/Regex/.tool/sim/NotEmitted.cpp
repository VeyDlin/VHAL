#include <Utilities/Regex/Regex.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>


// The file that CheckNotEmitted.sh compiles to an object. Without REGEX_USE_FIND it calls Matches only, so the captures loop must not be in
// the object. With REGEX_USE_FIND it calls Find, and the captures loop must be in the object: that proves the check can see the symbol.


using Pattern = Regex<+[] {
	return RegexBuilder("a(b|c)+d");
}>;


int main() {
#ifdef REGEX_USE_FIND
	Result<Pattern::Captures> found = Pattern::Find("abd");
	return found.IsOk() ? 0 : 1;
#else
	return Pattern::Matches("abd") ? 0 : 1;
#endif
}
