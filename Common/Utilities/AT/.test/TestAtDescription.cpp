#include <VHAL.h>
#include <Utilities/AT/AtCommandSet.h>
#include <Utilities/AT/Modules/AtDataMode.h>
#include <Utilities/CommandLine/CommandNameTree.h>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

// Host tests of the AT description: the names, the forms, the argument and reply types, the options, the URCs, the prefix tree, the dialect
// and the modules. Each expected value is the design's, written here by hand. Type expectations are static_asserts, and value
// expectations are checked at run time with a count of failures.


namespace {


int failureCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtDescription.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


void ExpectText(std::string_view actual, std::string_view expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtDescription.cpp:%d: %s is \"%.*s\", expected \"%.*s\"\n", line, expression,
			static_cast<int>(actual.size()), actual.data(), static_cast<int>(expected.size()), expected.data());
	}
}


#define AT_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)
#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)


// The description of the design's sensor example, without the Rows and data-mode parts.
using Sensor = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Modules(AtDataMode)
		.Dialect(AtDialect().Echo().Verbose().Quiet())
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Argument<String<64>>("password")).TimeoutMs(15000).Failures("FAIL")
		.Command<"+CWMODE">().Test().Read<int8>("mode").Write(Argument<int8>("mode"))
		.Command<"+CIFSR">().Run().Reply("+CIFSR:STAIP")
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Finals("SEND OK").Failures("SEND FAIL")
		.Basic<"Z">().Value(Argument<uint8>("on"))
		.Basic<"I">().Run()
		.Urc<"WIFI GOT IP">()
		.Urc<"+IPD">(Argument<uint16>("length"));
}>;


// A set with no defaults, dialect or modules, and an optional argument.
using Plain = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CWDHCP">().Write(Argument<uint8>("mode"), Optional<bool>("enable"));
}>;


// A set with a fixed CME mode and a CME text.
using Numeric = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().CmeErrors(AtCmeMode::numeric).CmeText<3, "operation not allowed">())
		.Command<"+GMR">().Run();
}>;


using CmeText3 = AtCmeTextItem<3, "operation not allowed">;


// A set with the CME command, which sets the mode at run time.
using CmeCmd = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().CmeCommand().Registers())
		.Command<"+GMR">().Run();
}>;


// The forms and their values, read from the chain before the set is built, so the argument names are checked too.
constexpr auto writeChain = AtCommandSetBuilder()
	.Command<"+LED">().Write(Argument<bool>("on"), Optional<uint8>("level"))
	.Close();


// A read of the names of the write form in the chain: items[0] is the command, forms[0] the write form.
constexpr std::string_view firstArgumentName = CommandPackGet<0>(CommandPackGet<0>(writeChain.items).forms).arguments.head.name;
constexpr std::string_view secondArgumentName = CommandPackGet<0>(CommandPackGet<0>(writeChain.items).forms).arguments.tail.head.name;


// Compile-time expectations: the types of the forms, the arguments and the replies.
static_assert(std::is_same_v<Sensor::CommandOf<"+TEMP">::ReadType, int16>);
static_assert(std::is_same_v<Sensor::CommandOf<"+CWMODE">::ReadType, int8>);
static_assert(std::is_same_v<Sensor::CommandOf<"+LED">::ReadType, void>);
static_assert(std::is_same_v<Sensor::CommandOf<"+LED">::WriteArguments, std::tuple<AtArgument<bool, false>>>);
static_assert(std::is_same_v<Sensor::CommandOf<"+JOIN">::WriteArguments,
	std::tuple<AtArgument<String<32>, false>, AtArgument<String<64>, false>>>);
static_assert(std::is_same_v<Sensor::CommandOf<"Z">::ValueArguments, std::tuple<AtArgument<uint8, false>>>);
static_assert(std::is_same_v<Sensor::UrcType<1>::ArgumentTypes, std::tuple<AtArgument<uint16, false>>>);
static_assert(std::is_same_v<AtArgument<String<8>, true>::HandlerType, std::optional<std::string_view>>);
static_assert(std::is_same_v<AtArgument<int16, false>::HandlerType, int16>);
static_assert(std::is_same_v<AtArgument<String<8>, false>::HandlerType, std::string_view>);
static_assert(std::is_same_v<Plain::CommandOf<"+CWDHCP">::WriteArguments,
	std::tuple<AtArgument<uint8, false>, AtArgument<bool, true>>>);

static_assert(Sensor::CommandOf<"+CWMODE">::CountOf<AtFormKind::test>() == 1);
static_assert(Sensor::CommandOf<"+CWMODE">::CountOf<AtFormKind::read>() == 1);
static_assert(Sensor::CommandOf<"+CWMODE">::CountOf<AtFormKind::write>() == 1);
static_assert(Sensor::CommandOf<"+CWMODE">::CountOf<AtFormKind::run>() == 0);
static_assert(Sensor::CommandOf<"+CIFSR">::CountOf<AtFormKind::run>() == 1);
static_assert(Sensor::commands[5].options.finalCount == 1);

static_assert(Sensor::CommandCount == 8);
static_assert(Sensor::UrcCount == 2);
static_assert(Sensor::Index<"+TEMP">() == 0);
static_assert(Sensor::Index<"+CIPSEND">() == 5);
static_assert(Sensor::Index<"Z">() == 6);
static_assert(Sensor::Index<"I">() == 7);
static_assert(Sensor::UrcIndex<"+IPD">() == 1);

static_assert(Sensor::Has<AtDataModeTag>);
static_assert(Sensor::Has<AtEchoItem>);
static_assert(Sensor::Has<AtVerboseItem>);
static_assert(Sensor::Has<AtQuietItem>);
static_assert(!Sensor::Has<AtRegistersItem>);
static_assert(!Sensor::Has<AtCmeCommandItem>);
static_assert(!Plain::Has<AtEchoItem>);
static_assert(!Plain::Has<AtDataModeTag>);

static_assert(!AtIsSupported<uint64>);
static_assert(!AtIsSupported<float>);
static_assert(!AtIsSupported<char>);
static_assert(AtIsSupported<String<1>>);
static_assert(AtIsSupported<bool>);
static_assert(AtIsSupported<int8>);


int TestNames() {
	int before = failureCount;

	AT_TEXT(Sensor::names[0], "+TEMP");
	AT_TEXT(Sensor::names[5], "+CIPSEND");
	AT_TEXT(Sensor::names[6], "Z");
	AT_TEXT(Sensor::urcNames[0], "WIFI GOT IP");
	AT_TEXT(Sensor::urcNames[1], "+IPD");
	AT_EQ(Sensor::commands[6].isBasic, true);
	AT_EQ(Sensor::commands[0].isBasic, false);
	AT_TEXT(Sensor::commands[0].name, "+TEMP");

	return failureCount - before;
}


int TestTimeouts() {
	int before = failureCount;

	AT_EQ(Sensor::DefaultTimeoutMs, 2000);
	AT_EQ(Sensor::commands[0].options.timeoutMs, 2000);
	AT_EQ(Sensor::commands[2].options.timeoutMs, 15000);
	AT_EQ(Plain::DefaultTimeoutMs, AtFallbackTimeoutMs);
	AT_EQ(Plain::commands[0].options.timeoutMs, AtFallbackTimeoutMs);
	AT_EQ(AtFallbackTimeoutMs, 1000);
	constexpr AtDefaults defaults = AtDefaults().TimeoutMs(2000);
	AT_EQ(defaults.timeoutMs, 2000);

	return failureCount - before;
}


int TestOptions() {
	int before = failureCount;

	AT_TEXT(Sensor::commands[4].options.replyPrefix, "+CIFSR:STAIP");
	AT_EQ(Sensor::commands[5].options.finalCount, 1);
	AT_TEXT(Sensor::commands[5].options.finals[0], "SEND OK");
	AT_EQ(Sensor::commands[5].options.failureCount, 1);
	AT_TEXT(Sensor::commands[5].options.failures[0], "SEND FAIL");
	AT_EQ(Sensor::commands[2].options.failureCount, 1);
	AT_TEXT(Sensor::commands[2].options.failures[0], "FAIL");
	AT_EQ(Sensor::commands[0].options.finalCount, 0);
	AT_TEXT(Sensor::commands[0].options.replyPrefix, "");

	return failureCount - before;
}


int TestArgumentNames() {
	int before = failureCount;

	AT_TEXT(firstArgumentName, "on");
	AT_TEXT(secondArgumentName, "level");
	AT_EQ(CommandPackGet<0>(CommandPackGet<0>(writeChain.items).forms).arguments.tail.head.isOptional, true);

	return failureCount - before;
}


int TestTree() {
	int before = failureCount;

	std::span<const CommandTreeNode> nodes(Sensor::tree.data(), Sensor::tree.size());

	uint16 node = 0;
	for (char character : std::string_view("+TEMP")) {
		node = CommandTreeStep(nodes, node, character);
	}
	AT_EQ(nodes[node].payload, 0);

	node = 0;
	for (char character : std::string_view("+CIPSEND")) {
		node = CommandTreeStep(nodes, node, character);
	}
	AT_EQ(nodes[node].payload, 5);

	node = 0;
	for (char character : std::string_view("+TM")) {
		node = CommandTreeStep(nodes, node, character);
	}
	AT_EQ(node, CommandNoNode);

	node = 1;
	for (char character : std::string_view("WIFI GOT IP")) {
		node = CommandTreeStep(nodes, node, character);
	}
	AT_EQ(nodes[node].payload, 0);

	node = 1;
	for (char character : std::string_view("+IPD")) {
		node = CommandTreeStep(nodes, node, character);
	}
	AT_EQ(nodes[node].payload, 1);

	node = 0;
	AT_EQ(CommandTreeStep(nodes, node, 'E') != CommandNoNode, true);

	return failureCount - before;
}


int TestDialectAndModes() {
	int before = failureCount;

	AT_EQ(Sensor::CmeMode, AtCmeMode::off);
	AT_EQ(Numeric::CmeMode, AtCmeMode::numeric);
	AT_EQ(CmeCmd::CmeMode, AtCmeMode::off);
	AT_EQ(CmeCmd::Has<AtCmeCommandItem>, true);
	AT_EQ(CmeCmd::Has<AtRegistersItem>, true);
	AT_EQ(Numeric::Has<CmeText3>, true);
	AT_EQ(Numeric::Has<AtEchoItem>, false);
	AT_TEXT(CmeText3::text, "operation not allowed");
	AT_EQ(CmeText3::code, 3);

	return failureCount - before;
}


int TestStatus() {
	int before = failureCount;

	AtStatus plain;
	AT_EQ(plain.status == ResultStatus::ok, true);
	AT_EQ(plain.cmeCode, 0);

	AtStatus cme = AtCmeError(3);
	AtStatus sameCme{.status = ResultStatus::error, .cmeCode = 3};
	AtStatus otherCme{.status = ResultStatus::error, .cmeCode = 4};
	AtStatus busy{ResultStatus::busy};
	AT_EQ(cme.status == ResultStatus::error, true);
	AT_EQ(cme.cmeCode, 3);
	AT_EQ(cme == sameCme, true);
	AT_EQ(cme == otherCme, false);
	AT_EQ(busy == AtStatus{ResultStatus::busy}, true);

	return failureCount - before;
}


} // namespace


int main() {
	TestNames();
	TestTimeouts();
	TestOptions();
	TestArgumentNames();
	TestTree();
	TestDialectAndModes();
	TestStatus();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtDescription: %d failure(s)\n", failureCount);
		return 1;
	}

	std::printf("TestAtDescription: all checks passed\n");
	return 0;
}
