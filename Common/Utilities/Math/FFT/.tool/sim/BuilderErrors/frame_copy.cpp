// expect: use of deleted function
#include <Utilities/Math/FFT/FrameCollector.h>


// A frame is a lease on one buffer, so it cannot be copied: a copy would release the buffer twice.
void Copy(FrameCollector<float, 4>::Frame &frame) {
	FrameCollector<float, 4>::Frame copy(frame);
	(void)copy;
}
