// expect: the files do not fit in the volume
#include <Utilities/FileSystem/Fat12ImageBuilder.h>
#include <array>

inline constexpr std::array<uint8, 2048> Big{};

using Image = Fat12Image<+[] {
	return Fat12ImageBuilder()
		.Sectors(16)
		.RootEntries(16)
		.File("f1.bin", std::span<const uint8>(Big))
		.File("f2.bin", std::span<const uint8>(Big))
		.File("f3.bin", std::span<const uint8>(Big))
		.File("f4.bin", std::span<const uint8>(Big));
}>;

static_assert(Image::Size > 0);
