// expect: a FAT12 image needs a size: call Sectors()
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.VolumeLabel("NO SIZE");
}>;

static_assert(Image::Size > 0);
