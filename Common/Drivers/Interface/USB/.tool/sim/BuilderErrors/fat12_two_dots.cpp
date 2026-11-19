// expect: a file name has at most one dot
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.File("a.b.txt", "x");
}>;

static_assert(Image::Size > 0);
