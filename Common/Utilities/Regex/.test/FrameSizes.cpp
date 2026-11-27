#include "FramePatterns.h"
#include <cstdio>


// Prints the workspace sizes of the two patterns of FramePatterns.h, one per line as "kind bytes". The sizes are the same on the host and
// on the device, because they count arrays of 2- and 4-byte elements.
int main() {
	std::printf("plain %zu\n", FrameSmall::PlainStackBytes);
	std::printf("plain %zu\n", FrameLarge::PlainStackBytes);
	std::printf("captures %zu\n", FrameSmall::CapturesStackBytes);
	std::printf("captures %zu\n", FrameLarge::CapturesStackBytes);
	return 0;
}
