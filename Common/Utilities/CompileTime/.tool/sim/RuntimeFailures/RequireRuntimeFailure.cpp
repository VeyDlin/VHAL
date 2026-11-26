#include <Utilities/CompileTime/Require.h>
#include <Utilities/CompileTime/.tool/sim/CompileTimeError.h>
#include <cstring>


// Runs a check whose condition is false at run time. The host definition prints the message and aborts, so the program never
// reaches its return. The first argument picks the check: "value" calls RequireValue, anything else calls Require.
int main(int argc, char** argv) {
	if (argc == 2 && std::strcmp(argv[1], "value") == 0) {
		RequireValue<"CompileTime: value failure", 7>(false);
	}

	Require<"CompileTime: run-time failure">(false);
	return 0;
}
