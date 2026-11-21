// expect: a custom argument parser must be trivially destructible
#include "CommandErrorApp.h"


struct ErrOwned;


// A parser with a destructor. Its slot is placed in the console's storage and never destroyed, so the builder refuses it.
struct OwningParser {
	~OwningParser() {
	}


	bool Push(char character) {
		static_cast<void>(character);
		return true;
	}


	void Pop() {
	}


	bool Finish(ErrOwned& value) const {
		static_cast<void>(value);
		return true;
	}
};


struct ErrOwned {
	using CommandParser = OwningParser;
};


inline ResultStatus ErrTake(CommandCall<ErrApp> call, ErrOwned value) {
	static_cast<void>(call);
	static_cast<void>(value);
	return ResultStatus::ok;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("take", "take a custom value", &ErrTake, Argument<ErrOwned>("value"));
}>;

static_assert(Set::CommandCount == 1);
