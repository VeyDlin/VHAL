// expect: a FAT has 1 or 2 copies
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.Fats(3);
}>;

static_assert(Image::Size > 0);
