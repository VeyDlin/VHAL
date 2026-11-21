#include "ProcessPoll.h"


PollStep DecidePollStep(bool exited, bool outputReady, bool deadlinePassed) {
	if (exited && !outputReady) {
		return PollStep::Finish;
	}

	if (deadlinePassed) {
		return PollStep::Timeout;
	}

	if (outputReady) {
		return PollStep::ReadOutput;
	}

	return PollStep::Wait;
}
