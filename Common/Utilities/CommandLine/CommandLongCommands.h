#pragma once
#include "CommandModules.h"


// The LongCommands module. A command whose handler returns CommandStep runs over several Process passes: it returns CommandContinue to run
// again on the next pass, and a ResultStatus to finish. Its first parameter is a CommandLongCall, which carries the handler's state:
//
//   using App = CommandLine<AppCommands, AppContext, Help, LongCommands>;
//
//   struct TypeState {
//       uint32 sent = 0;
//   };
//
//   CommandStep Type(CommandLongCall<AppContext, TypeState> call, uint8 count) {
//       if (call.cancelled) {
//           return ResultStatus::ok;             // the one call Ctrl+C makes; the step is ignored
//       }
//
//       if (call.state.sent == count) {
//           return ResultStatus::ok;
//       }
//
//       call.out.Write("*");
//       call.state.sent++;
//       return CommandContinue;
//   }
//
// The state is made before the first call, and kept until the command ends. It must be trivially destructible. A long command has one
// state at a time, in a slot the console sizes for the largest state of the set.
//
// While a command runs, the console runs its next step once on each Process pass, and the input waits in the ring. Input that arrives
// meanwhile is read after the command, up to the ring's size (32 bytes, or InputRing<N>); more is lost, and the console raises InputOverflow.
// Ctrl+C cancels the command: its handler gets one call with cancelled set, the bytes typed before the Ctrl+C are dropped, and the
// Interrupt event follows. A command that ends with a status other than ok raises CommandFailed, as an ordinary command does.
//
// The console does the running (the run state is kept in the console, sized from the set), so this module is a feature rather than a hook:
// a set with a long command needs this module listed, a compile error otherwise. A set without a long command has no run code, so the
// module costs nothing when it is not listed.


struct LongCommands {
	using Provides = CommandLongFeature;
};
