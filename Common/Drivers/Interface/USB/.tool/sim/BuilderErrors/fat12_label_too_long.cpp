// expect: the volume label has 1 to 11 characters
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.VolumeLabel("TWELVE CHARS");
}>;

static_assert(Image::Size > 0);
