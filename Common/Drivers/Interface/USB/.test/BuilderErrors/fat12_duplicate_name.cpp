// expect: two files have the same short name
#include <Utilities/FileSystem/Fat12ImageBuilder.h>

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(128)
		.File("a.txt", "x")
		.File("A.TXT", "y");
}>;

static_assert(Image::Size > 0);
