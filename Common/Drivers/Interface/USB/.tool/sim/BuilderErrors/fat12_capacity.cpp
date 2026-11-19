// expect: the image is larger than the builder capacity of 512 sectors
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(1024);
}>;

static_assert(Image::Size > 0);
