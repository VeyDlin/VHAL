#include <Utilities/Regex/Regex.h>
#include <Utilities/Result/Result.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>


// The match tests with a global operator new that aborts. A call that reaches the heap ends the process, so a pass means the matching
// paths use no heap.


void* operator new(std::size_t) {
	std::puts("operator new was called");
	std::abort();
}


void* operator new[](std::size_t) {
	std::puts("operator new[] was called");
	std::abort();
}


void operator delete(void*) noexcept {
}


void operator delete(void*, std::size_t) noexcept {
}


void operator delete[](void*) noexcept {
}


void operator delete[](void*, std::size_t) noexcept {
}


namespace {


int failures = 0;


void Expect(int line, bool ok) {
	if (!ok) {
		failures++;
		std::printf("FAIL line %d\n", line);
	}
}


}


int main() {
	using Url = Regex<+[] {
		return RegexBuilder(R"(^(https?)://([a-z0-9.-]+)(?::([0-9]{1,5}))?(/[^ ]*)?$)").IgnoreCase().StackLimit(3072);
	}>;
	using Status = Regex<+[] {
		return RegexBuilder(R"(HTTP/1\.[01] [0-9]{3}( .*)?)");
	}>;
	using Alternate = Regex<+[] {
		return RegexBuilder("(a|ab)(c|bcd)(d*)");
	}>;
	using Lazy = Regex<+[] {
		return RegexBuilder("(a+?)(b*)c");
	}>;

	Expect(__LINE__, Url::Matches("https://example.com:8443/path?x=1"));
	Expect(__LINE__, !Url::Matches("ftp://example.com"));
	Expect(__LINE__, Status::Matches("HTTP/1.1 200 OK"));
	Expect(__LINE__, Status::Matches("HTTP/1.0 404"));
	Expect(__LINE__, !Status::Matches("HTTP/2.0 200"));
	Expect(__LINE__, Alternate::Matches("abcd"));
	Expect(__LINE__, Lazy::Contains("xaabbbcx"));

	Result<Url::Captures> found = Url::Find("HTTP://example.com:8080/a");
	Expect(__LINE__, found.IsOk() && found.Value().Group<2>() == "example.com");
	Expect(__LINE__, found.IsOk() && found.Value().Group<3>() == "8080");

	Result<Alternate::Captures> leftmost = Alternate::Find("abcd");
	Expect(__LINE__, leftmost.IsOk() && leftmost.Value().Group<0>() == "abcd");
	Expect(__LINE__, leftmost.IsOk() && leftmost.Value().Group<1>() == "a");

	Result<Lazy::Captures> lazy = Lazy::Find("xaabbbcx");
	Expect(__LINE__, lazy.IsOk() && lazy.Value().Group<1>() == "aa");
	Expect(__LINE__, lazy.IsOk() && lazy.Value().Group<2>() == "bbb");

	std::printf("no heap: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
