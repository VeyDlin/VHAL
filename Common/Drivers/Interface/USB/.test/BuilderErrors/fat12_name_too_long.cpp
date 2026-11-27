// expect: a short name base has 1 to 8 characters
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.File("toolongname.txt", "x");
}>;

static_assert(Image::Size > 0);
