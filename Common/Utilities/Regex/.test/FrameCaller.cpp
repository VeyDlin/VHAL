#include "FramePatterns.h"
#include <string_view>


// One caller that makes the six calls. Its frame must not hold a workspace, so the workspaces are in the frames of the calls.
uint32 FrameCaller(std::string_view text) {
	uint32 result = 0;
	result += FrameSmall::Matches(text) ? 1u : 0u;
	result += FrameSmall::Contains(text) ? 2u : 0u;
	Result<FrameSmall::Captures> smallFound = FrameSmall::Find(text);
	result += smallFound.IsOk() ? 4u : 0u;
	result += FrameLarge::Matches(text) ? 8u : 0u;
	result += FrameLarge::Contains(text) ? 16u : 0u;
	Result<FrameLarge::Captures> largeFound = FrameLarge::Find(text);
	result += largeFound.IsOk() ? 32u : 0u;
	return result;
}
