#include <Utilities/DataTypes.h>
#include <Utilities/Regex/Regex.h>
#include <cstddef>
#include <cstdio>
#include <string_view>


// The fixture rows of the board demo, run on the host. The rows come from std::regex (fixtures/WriteBoardFixtures.cpp), so this test
// checks the matcher against std::regex through the same rows the board runs. The board check is the same loop with the board's output.


namespace BoardFixtures {
struct BoardRow {
	uint8 pattern;
	const char* input;
	bool matches;
	bool contains;
	const char* group1;
	const char* group2;
};


#include <Utilities/Regex/.tool/sim/fixtures/Board.inc>


using StatusLine = Regex<+[] {
	return RegexBuilder(BoardStatusPattern);
}>;


using Url = Regex<+[] {
	return RegexBuilder(BoardUrlPattern).IgnoreCase().StackLimit(3072);
}>;


// The checks of one row against the pattern it names. Returns the number of mismatches, and prints one line for each.
template<typename Pattern>
uint32 CheckRowWith(const BoardRow& row, std::size_t index) {
	std::string_view text(row.input);
	uint32 mismatches = 0;

	if (Pattern::Matches(text) != row.matches) {
		mismatches++;
		std::printf("row %zu Matches differs\n", index);
	}

	if (Pattern::Contains(text) != row.contains) {
		mismatches++;
		std::printf("row %zu Contains differs\n", index);
	}

	Result<typename Pattern::Captures> found = Pattern::Find(text);
	if (found.IsOk() != row.contains) {
		mismatches++;
		std::printf("row %zu Find differs\n", index);
	}

	if (found.IsOk()) {
		if (found.Value().Group(1) != std::string_view(row.group1)) {
			mismatches++;
			std::printf("row %zu group 1 differs\n", index);
		}

		if (found.Value().Group(2) != std::string_view(row.group2)) {
			mismatches++;
			std::printf("row %zu group 2 differs\n", index);
		}
	}

	return mismatches;
}
}


int main() {
	std::size_t rowCount = sizeof(BoardFixtures::BoardRows) / sizeof(BoardFixtures::BoardRows[0]);
	uint32 mismatches = 0;
	for (std::size_t index = 0; index < rowCount; index++) {
		const BoardFixtures::BoardRow& row = BoardFixtures::BoardRows[index];
		if (row.pattern == 0) {
			mismatches += BoardFixtures::CheckRowWith<BoardFixtures::StatusLine>(row, index);
		} else {
			mismatches += BoardFixtures::CheckRowWith<BoardFixtures::Url>(row, index);
		}
	}

	std::printf("board fixtures: %zu rows, %u mismatch(es)\n", rowCount, static_cast<unsigned>(mismatches));
	return mismatches == 0 ? 0 : 1;
}
