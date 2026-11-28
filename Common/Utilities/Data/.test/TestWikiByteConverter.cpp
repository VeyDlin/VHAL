#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Data/ByteConverter.h>

#include <array>
#include <span>


// The snippets of the ByteConverter wiki page for the byte order forms. Each snippet is the page's code; a check follows it.


namespace TypedValuesPage {
	void Run() {
		uint8 frame[8] = {};
		ByteConverter::Put<uint16, ByteOrder::little>(frame, 0, uint16(0x1234));   // 34 12
		ByteConverter::Put<uint32, ByteOrder::big>(frame, 2, uint32(0xAABBCCDD));  // AA BB CC DD at 2..5
		int16 signedWord = ByteConverter::Get<int16, ByteOrder::big>(frame, 2);    // 0xAABB, which is negative as int16
		VHAL_CHECK_EQ(frame[0], static_cast<uint8>(0x34));
		VHAL_CHECK_EQ(frame[1], static_cast<uint8>(0x12));
		VHAL_CHECK_EQ(frame[2], static_cast<uint8>(0xAA));
		VHAL_CHECK_EQ(signedWord, static_cast<int16>(0xAABB));

		std::array<uint8, 4> packet{};
		ByteConverter::Put<uint16, ByteOrder::big>(packet, 0, uint16(0x1234));    // span form of a std::array
		uint16 back = ByteConverter::Get<uint16, ByteOrder::big>(packet, 0);      // 0x1234
		VHAL_CHECK_EQ(back, static_cast<uint16>(0x1234));
	}
}


namespace WidthFormsPage {
	void Run() {
		uint8 frame[8] = {};
		ByteConverter::Put<3, ByteOrder::little>(frame, 0, uint64(0x123456));     // 56 34 12
		uint64 field = ByteConverter::Get<3, ByteOrder::little>(frame, 0);        // 0x123456
		VHAL_CHECK_EQ(frame[0], static_cast<uint8>(0x56));
		VHAL_CHECK_EQ(frame[2], static_cast<uint8>(0x12));
		VHAL_CHECK_EQ(field, static_cast<uint64>(0x123456));

		ByteConverter::Put<ByteOrder::big>(frame, 0, uint64(0xABCD), 2);          // AB CD
		uint64 read = ByteConverter::Get<ByteOrder::big>(frame, 0, 2);            // 0xABCD
		VHAL_CHECK_EQ(frame[0], static_cast<uint8>(0xAB));
		VHAL_CHECK_EQ(frame[1], static_cast<uint8>(0xCD));
		VHAL_CHECK_EQ(read, static_cast<uint64>(0xABCD));
	}
}


namespace ArraysPage {
	void Run() {
		std::array<uint8, 2> raw = ByteConverter::ToBytes<uint16, ByteOrder::big>(uint16(0x1234));   // 12 34
		uint16 again = ByteConverter::FromBytes<uint16, ByteOrder::big>(raw);                        // 0x1234
		VHAL_CHECK_EQ(raw[0], static_cast<uint8>(0x12));
		VHAL_CHECK_EQ(again, static_cast<uint16>(0x1234));

		constexpr std::array<uint8, 2> packed = ByteConverter::ToBytes<uint16, ByteOrder::little>(uint16(0x1234));   // 34 12
		static_assert(packed[0] == 0x34);
		VHAL_CHECK_EQ(packed[1], static_cast<uint8>(0x12));
	}
}


int main() {
	TypedValuesPage::Run();
	WidthFormsPage::Run();
	ArraysPage::Run();

	return VhalCheck::Finish("TestWikiByteConverter");
}
