#include <DemoRegex/DemoRegex.h>
#include <Utilities/Regex/Regex.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>


// The linker symbols of STM32G431XB_FLASH.ld: bssEnd is _end, the end of .bss, where the free RAM starts, and stackTop is _estack, the top
// of RAM, where the stack starts. The stack grows down from stackTop towards bssEnd.
extern "C" {
	extern char bssEnd[] asm("_end");
	extern char stackTop[] asm("_estack");
}


// The SWD reader sees these. The verdict is the number of mismatches, and the stack bytes are the stack the last run used.
volatile uint32 regexMismatches __attribute__((used)) = 0;
volatile uint32 regexStackUsed __attribute__((used)) = 0;


namespace DemoRegex {
// One fixture row: the pattern it names (0 is the status line, 1 the URL), the input and the expected results of Matches, Contains and
// the groups of Find. The row list is generated, see Common/Utilities/Regex/.test/fixtures/WriteBoardFixtures.cpp.
struct BoardRow {
	uint8 pattern;
	const char* input;
	bool matches;
	bool contains;
	const char* group1;
	const char* group2;
};


#include <Utilities/Regex/.test/fixtures/Board.inc>


// The patterns of the board check. They are the fixture patterns, so the board and the host read the same text.
using StatusLine = Regex<+[] {
	return RegexBuilder(BoardStatusPattern);
}>;


using Url = Regex<+[] {
	return RegexBuilder(BoardUrlPattern).IgnoreCase().StackLimit(3072);
}>;


constexpr uint8 PaintByte = 0xA5;
// The stack bytes that the budget allows on top of the workspace, from the frames above and below the stack pointer of MeasureRun. The
// frames are from -fstack-usage at -O3 for the Cortex-M4 build of this file, and the sum is rounded up to the next 32 bytes:
//   main                                                     8 bytes
//   Run, with MeasureRun, CheckRows and CheckRowWith inlined   56 bytes Matches, 112 bytes Find
//   the entry point of the largest pattern, without its workspace (Url)
//                                                            80 bytes Matches, 224 bytes Find
//   an interrupt at the deepest point: the exception frame with the FPU context (26 words, the build uses the hard-float ABI)
//                                                            104 bytes
//   the interrupt handler's own frame (SysTick tail-calls the tick counter and has none; the allowance covers a deeper handler)
//                                                            32 bytes
// Matches sums to 280 bytes and rounds up to 288. Find sums to 480 bytes.
#if defined(VHAL_REGEX_FIND)
constexpr std::size_t StackMarginBytes = 480;
#else
constexpr std::size_t StackMarginBytes = 288;
#endif

// The stack budget of the variant: the larger of the two patterns' workspaces for the calls the variant makes.
#if defined(VHAL_REGEX_FIND)
constexpr std::size_t StackBudgetBytes = (StatusLine::CapturesStackBytes > Url::CapturesStackBytes)
	? StatusLine::CapturesStackBytes
	: Url::CapturesStackBytes;
#else
constexpr std::size_t StackBudgetBytes = (StatusLine::PlainStackBytes > Url::PlainStackBytes)
	? StatusLine::PlainStackBytes
	: Url::PlainStackBytes;
#endif

constexpr uint32 BoardRowCount = static_cast<uint32>(std::size(BoardRows));

// The checks of a row that differ from its expected results, one bit each.
constexpr uint8 FailMatches = 0x01;
constexpr uint8 FailContains = 0x02;
constexpr uint8 FailFind = 0x04;
constexpr uint8 FailGroup1 = 0x08;
constexpr uint8 FailGroup2 = 0x10;

// The failed checks of each row in the last run. They are written during the measure and printed after it, so that the printing does not
// use the stack that is measured.
static uint8 rowFailures[BoardRowCount];


// The stack pointer of the function that calls this.
static std::uintptr_t CurrentStackPointer() {
	return static_cast<std::uintptr_t>(__get_MSP());
}


// Fills the free RAM from bssEnd up to top with PaintByte. The writes are volatile, so the optimizer keeps each one.
static void PaintStack(std::uintptr_t top) {
	for (std::uintptr_t address = reinterpret_cast<std::uintptr_t>(bssEnd); address < top; address++) {
		*reinterpret_cast<volatile uint8*>(address) = PaintByte;
	}
}


// The lowest address below top that does not hold PaintByte. The stack wrote there, so it is the deepest the stack went. Returns top
// when nothing below top changed.
static std::uintptr_t LowestChangedAddress(std::uintptr_t top) {
	for (std::uintptr_t address = reinterpret_cast<std::uintptr_t>(bssEnd); address < top; address++) {
		if (*reinterpret_cast<volatile uint8*>(address) != PaintByte) {
			return address;
		}
	}

	return top;
}


// Prints one line for a check that differs from its fixture row.
static void ReportMismatch(uint32 row, const char* check) {
	System::console << "row " << row << " " << check << " differs" << Console::endl;
}


// The checks of one row against the pattern it names. Find runs only in the variant that calls it. Returns the failed checks as bits.
template<typename Pattern>
static uint8 CheckRowWith(const BoardRow& row) {
	std::string_view text(row.input);
	uint8 failed = 0;

	if (Pattern::Matches(text) != row.matches) {
		failed = static_cast<uint8>(failed | FailMatches);
	}

	if (Pattern::Contains(text) != row.contains) {
		failed = static_cast<uint8>(failed | FailContains);
	}

#if defined(VHAL_REGEX_FIND)
	Result<typename Pattern::Captures> found = Pattern::Find(text);
	if (found.IsOk() != row.contains) {
		failed = static_cast<uint8>(failed | FailFind);
	}

	if (found.IsOk()) {
		if (found.Value().Group(1) != std::string_view(row.group1)) {
			failed = static_cast<uint8>(failed | FailGroup1);
		}

		if (found.Value().Group(2) != std::string_view(row.group2)) {
			failed = static_cast<uint8>(failed | FailGroup2);
		}
	}
#endif

	return failed;
}


// The number of bits set in a set of failed checks.
static uint32 CountFailures(uint8 failed) {
	uint32 count = 0;
	for (uint8 bit = 1; bit != 0; bit = static_cast<uint8>(bit << 1)) {
		if ((failed & bit) != 0) {
			count++;
		}
	}

	return count;
}


// All rows of the fixture list. The failed checks of each row go to rowFailures. Returns the number of mismatches.
static uint32 CheckRows() {
	uint32 mismatches = 0;
	for (uint32 index = 0; index < BoardRowCount; index++) {
		const BoardRow& row = BoardRows[index];
		uint8 failed = 0;
		if (row.pattern == 0) {
			failed = CheckRowWith<StatusLine>(row);
		} else {
			failed = CheckRowWith<Url>(row);
		}

		rowFailures[index] = failed;
		mismatches += CountFailures(failed);
	}

	return mismatches;
}


// Prints one line for each failed check of the last run.
static void ReportFailures() {
	for (uint32 index = 0; index < BoardRowCount; index++) {
		uint8 failed = rowFailures[index];
		if ((failed & FailMatches) != 0) {
			ReportMismatch(index, "Matches");
		}

		if ((failed & FailContains) != 0) {
			ReportMismatch(index, "Contains");
		}

		if ((failed & FailFind) != 0) {
			ReportMismatch(index, "Find");
		}

		if ((failed & FailGroup1) != 0) {
			ReportMismatch(index, "group 1");
		}

		if ((failed & FailGroup2) != 0) {
			ReportMismatch(index, "group 2");
		}
	}
}


// Runs the rows with the free RAM below the stack pointer painted, and returns the lowest changed address. The paint top is the stack
// pointer less the margin, so the frames of this function stay unpainted.
static std::uintptr_t MeasureRun(uint32& mismatches) {
	std::uintptr_t top = CurrentStackPointer() - StackMarginBytes;
	PaintStack(top);
	mismatches = CheckRows();
	return LowestChangedAddress(top);
}


void Run(ResultStatus consoleStatus) {
	static_cast<void>(consoleStatus);
	uint32 mismatches = 0;
	std::uintptr_t lowest = MeasureRun(mismatches);

	// The stack bytes are the distance from stackTop to the lowest changed byte. The stack reached bssEnd when that byte is bssEnd itself,
	// because the paint starts there.
	std::size_t stackUsed = reinterpret_cast<std::uintptr_t>(stackTop) - lowest;
	bool reachedEnd = lowest <= reinterpret_cast<std::uintptr_t>(bssEnd);
	bool withinBudget = stackUsed <= StackBudgetBytes + StackMarginBytes;
	bool passed = mismatches == 0 && !reachedEnd && withinBudget;
	regexMismatches = mismatches;
	regexStackUsed = static_cast<uint32>(stackUsed);

	ReportFailures();
	System::console << "regex rows " << BoardRowCount << " mismatches " << mismatches << Console::endl;
	System::console << "stack used " << static_cast<uint32>(stackUsed) << " budget " << static_cast<uint32>(StackBudgetBytes)
		<< " margin " << static_cast<uint32>(StackMarginBytes) << Console::endl;
	if (reachedEnd) {
		System::console << "stack reached the end of bss" << Console::endl;
	}

	System::console << (passed ? "regex result: pass" : "regex result: FAIL") << Console::endl;

	while (true) {
		System::DelayMs(1000);
	}
}
}
