#pragma once
#include <Utilities/Regex/Regex.h>


// Two patterns of different sizes, the board status line and the board URL. CheckFrames.cmake makes the calls of both from one caller, so
// that each workspace is checked against the frame of its own call.
using FrameSmall = Regex<+[] {
	return RegexBuilder(R"(HTTP/1\.[01] [0-9]{3}( .*)?)");
}>;


using FrameLarge = Regex<+[] {
	return RegexBuilder(R"(^(https?)://([a-z0-9.-]+)(?::([0-9]{1,5}))?(/[^ ]*)?$)").IgnoreCase();
}>;
