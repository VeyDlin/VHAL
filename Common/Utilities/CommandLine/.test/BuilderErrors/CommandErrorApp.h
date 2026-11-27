#pragma once
#include <Utilities/CommandLine/CommandSet.h>


// The shared pieces of the compile-fail snippets: an application type, a handler, a default handler and an interrupt handler.
// Each snippet builds one chain that breaks one rule, and forces the set to be built by naming its command count.


struct ErrApp {
	int value = 0;
};


enum class ErrMode : uint8 {
	Fast,
	Slow
};


inline ResultStatus ErrKey(CommandCall<ErrApp> call, uint8 code) {
	call.app.value = code;
	return ResultStatus::ok;
}


inline ResultStatus ErrText(CommandCall<ErrApp> call, String<4> text) {
	call.app.value = static_cast<int>(text.length);
	return ResultStatus::ok;
}


inline ResultStatus ErrSetMode(CommandCall<ErrApp> call, ErrMode mode) {
	call.app.value = static_cast<int>(mode);
	return ResultStatus::ok;
}


inline ResultStatus ErrPair(CommandCall<ErrApp> call, uint8 first, uint8 second) {
	call.app.value = first + second;
	return ResultStatus::ok;
}


inline void ErrEvent(CommandCall<ErrApp> call, const CommandEvent& event) {
	static_cast<void>(call);
	static_cast<void>(event);
}


inline void ErrStop(ErrApp& app) {
	app.value = 0;
}
