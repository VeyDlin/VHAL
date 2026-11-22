// expect: FrameCollector: Hop must be in 1..Size
#include <Utilities/Math/FFT/FrameCollector.h>


// A hop longer than the frame would skip samples, so it is rejected.
FrameCollector<float, 8, 9> frames;
