// expect: sectors per cluster is a power of two from 1 to 128
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.SectorsPerCluster(3);
}>;

static_assert(Image::Size > 0);
