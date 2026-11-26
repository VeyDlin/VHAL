// expect: more than 240 commands
#include <Drivers/Interface/VUB/Requests/Commands.h>


// 241 values of one enum: the ids are one byte, so the last one repeats the first value.
enum class SnippetCommand : uint8 {
	c000 = 0x00,
	c001 = 0x01,
	c002 = 0x02,
	c003 = 0x03,
	c004 = 0x04,
	c005 = 0x05,
	c006 = 0x06,
	c007 = 0x07,
	c008 = 0x08,
	c009 = 0x09,
	c010 = 0x0A,
	c011 = 0x0B,
	c012 = 0x0C,
	c013 = 0x0D,
	c014 = 0x0E,
	c015 = 0x0F,
	c016 = 0x10,
	c017 = 0x11,
	c018 = 0x12,
	c019 = 0x13,
	c020 = 0x14,
	c021 = 0x15,
	c022 = 0x16,
	c023 = 0x17,
	c024 = 0x18,
	c025 = 0x19,
	c026 = 0x1A,
	c027 = 0x1B,
	c028 = 0x1C,
	c029 = 0x1D,
	c030 = 0x1E,
	c031 = 0x1F,
	c032 = 0x20,
	c033 = 0x21,
	c034 = 0x22,
	c035 = 0x23,
	c036 = 0x24,
	c037 = 0x25,
	c038 = 0x26,
	c039 = 0x27,
	c040 = 0x28,
	c041 = 0x29,
	c042 = 0x2A,
	c043 = 0x2B,
	c044 = 0x2C,
	c045 = 0x2D,
	c046 = 0x2E,
	c047 = 0x2F,
	c048 = 0x30,
	c049 = 0x31,
	c050 = 0x32,
	c051 = 0x33,
	c052 = 0x34,
	c053 = 0x35,
	c054 = 0x36,
	c055 = 0x37,
	c056 = 0x38,
	c057 = 0x39,
	c058 = 0x3A,
	c059 = 0x3B,
	c060 = 0x3C,
	c061 = 0x3D,
	c062 = 0x3E,
	c063 = 0x3F,
	c064 = 0x40,
	c065 = 0x41,
	c066 = 0x42,
	c067 = 0x43,
	c068 = 0x44,
	c069 = 0x45,
	c070 = 0x46,
	c071 = 0x47,
	c072 = 0x48,
	c073 = 0x49,
	c074 = 0x4A,
	c075 = 0x4B,
	c076 = 0x4C,
	c077 = 0x4D,
	c078 = 0x4E,
	c079 = 0x4F,
	c080 = 0x50,
	c081 = 0x51,
	c082 = 0x52,
	c083 = 0x53,
	c084 = 0x54,
	c085 = 0x55,
	c086 = 0x56,
	c087 = 0x57,
	c088 = 0x58,
	c089 = 0x59,
	c090 = 0x5A,
	c091 = 0x5B,
	c092 = 0x5C,
	c093 = 0x5D,
	c094 = 0x5E,
	c095 = 0x5F,
	c096 = 0x60,
	c097 = 0x61,
	c098 = 0x62,
	c099 = 0x63,
	c100 = 0x64,
	c101 = 0x65,
	c102 = 0x66,
	c103 = 0x67,
	c104 = 0x68,
	c105 = 0x69,
	c106 = 0x6A,
	c107 = 0x6B,
	c108 = 0x6C,
	c109 = 0x6D,
	c110 = 0x6E,
	c111 = 0x6F,
	c112 = 0x70,
	c113 = 0x71,
	c114 = 0x72,
	c115 = 0x73,
	c116 = 0x74,
	c117 = 0x75,
	c118 = 0x76,
	c119 = 0x77,
	c120 = 0x78,
	c121 = 0x79,
	c122 = 0x7A,
	c123 = 0x7B,
	c124 = 0x7C,
	c125 = 0x7D,
	c126 = 0x7E,
	c127 = 0x7F,
	c128 = 0x80,
	c129 = 0x81,
	c130 = 0x82,
	c131 = 0x83,
	c132 = 0x84,
	c133 = 0x85,
	c134 = 0x86,
	c135 = 0x87,
	c136 = 0x88,
	c137 = 0x89,
	c138 = 0x8A,
	c139 = 0x8B,
	c140 = 0x8C,
	c141 = 0x8D,
	c142 = 0x8E,
	c143 = 0x8F,
	c144 = 0x90,
	c145 = 0x91,
	c146 = 0x92,
	c147 = 0x93,
	c148 = 0x94,
	c149 = 0x95,
	c150 = 0x96,
	c151 = 0x97,
	c152 = 0x98,
	c153 = 0x99,
	c154 = 0x9A,
	c155 = 0x9B,
	c156 = 0x9C,
	c157 = 0x9D,
	c158 = 0x9E,
	c159 = 0x9F,
	c160 = 0xA0,
	c161 = 0xA1,
	c162 = 0xA2,
	c163 = 0xA3,
	c164 = 0xA4,
	c165 = 0xA5,
	c166 = 0xA6,
	c167 = 0xA7,
	c168 = 0xA8,
	c169 = 0xA9,
	c170 = 0xAA,
	c171 = 0xAB,
	c172 = 0xAC,
	c173 = 0xAD,
	c174 = 0xAE,
	c175 = 0xAF,
	c176 = 0xB0,
	c177 = 0xB1,
	c178 = 0xB2,
	c179 = 0xB3,
	c180 = 0xB4,
	c181 = 0xB5,
	c182 = 0xB6,
	c183 = 0xB7,
	c184 = 0xB8,
	c185 = 0xB9,
	c186 = 0xBA,
	c187 = 0xBB,
	c188 = 0xBC,
	c189 = 0xBD,
	c190 = 0xBE,
	c191 = 0xBF,
	c192 = 0xC0,
	c193 = 0xC1,
	c194 = 0xC2,
	c195 = 0xC3,
	c196 = 0xC4,
	c197 = 0xC5,
	c198 = 0xC6,
	c199 = 0xC7,
	c200 = 0xC8,
	c201 = 0xC9,
	c202 = 0xCA,
	c203 = 0xCB,
	c204 = 0xCC,
	c205 = 0xCD,
	c206 = 0xCE,
	c207 = 0xCF,
	c208 = 0xD0,
	c209 = 0xD1,
	c210 = 0xD2,
	c211 = 0xD3,
	c212 = 0xD4,
	c213 = 0xD5,
	c214 = 0xD6,
	c215 = 0xD7,
	c216 = 0xD8,
	c217 = 0xD9,
	c218 = 0xDA,
	c219 = 0xDB,
	c220 = 0xDC,
	c221 = 0xDD,
	c222 = 0xDE,
	c223 = 0xDF,
	c224 = 0xE0,
	c225 = 0xE1,
	c226 = 0xE2,
	c227 = 0xE3,
	c228 = 0xE4,
	c229 = 0xE5,
	c230 = 0xE6,
	c231 = 0xE7,
	c232 = 0xE8,
	c233 = 0xE9,
	c234 = 0xEA,
	c235 = 0xEB,
	c236 = 0xEC,
	c237 = 0xED,
	c238 = 0xEE,
	c239 = 0xEF,
	c240 = 0x00
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::c000>().NoReply()
		.Command<SnippetCommand::c001>().NoReply()
		.Command<SnippetCommand::c002>().NoReply()
		.Command<SnippetCommand::c003>().NoReply()
		.Command<SnippetCommand::c004>().NoReply()
		.Command<SnippetCommand::c005>().NoReply()
		.Command<SnippetCommand::c006>().NoReply()
		.Command<SnippetCommand::c007>().NoReply()
		.Command<SnippetCommand::c008>().NoReply()
		.Command<SnippetCommand::c009>().NoReply()
		.Command<SnippetCommand::c010>().NoReply()
		.Command<SnippetCommand::c011>().NoReply()
		.Command<SnippetCommand::c012>().NoReply()
		.Command<SnippetCommand::c013>().NoReply()
		.Command<SnippetCommand::c014>().NoReply()
		.Command<SnippetCommand::c015>().NoReply()
		.Command<SnippetCommand::c016>().NoReply()
		.Command<SnippetCommand::c017>().NoReply()
		.Command<SnippetCommand::c018>().NoReply()
		.Command<SnippetCommand::c019>().NoReply()
		.Command<SnippetCommand::c020>().NoReply()
		.Command<SnippetCommand::c021>().NoReply()
		.Command<SnippetCommand::c022>().NoReply()
		.Command<SnippetCommand::c023>().NoReply()
		.Command<SnippetCommand::c024>().NoReply()
		.Command<SnippetCommand::c025>().NoReply()
		.Command<SnippetCommand::c026>().NoReply()
		.Command<SnippetCommand::c027>().NoReply()
		.Command<SnippetCommand::c028>().NoReply()
		.Command<SnippetCommand::c029>().NoReply()
		.Command<SnippetCommand::c030>().NoReply()
		.Command<SnippetCommand::c031>().NoReply()
		.Command<SnippetCommand::c032>().NoReply()
		.Command<SnippetCommand::c033>().NoReply()
		.Command<SnippetCommand::c034>().NoReply()
		.Command<SnippetCommand::c035>().NoReply()
		.Command<SnippetCommand::c036>().NoReply()
		.Command<SnippetCommand::c037>().NoReply()
		.Command<SnippetCommand::c038>().NoReply()
		.Command<SnippetCommand::c039>().NoReply()
		.Command<SnippetCommand::c040>().NoReply()
		.Command<SnippetCommand::c041>().NoReply()
		.Command<SnippetCommand::c042>().NoReply()
		.Command<SnippetCommand::c043>().NoReply()
		.Command<SnippetCommand::c044>().NoReply()
		.Command<SnippetCommand::c045>().NoReply()
		.Command<SnippetCommand::c046>().NoReply()
		.Command<SnippetCommand::c047>().NoReply()
		.Command<SnippetCommand::c048>().NoReply()
		.Command<SnippetCommand::c049>().NoReply()
		.Command<SnippetCommand::c050>().NoReply()
		.Command<SnippetCommand::c051>().NoReply()
		.Command<SnippetCommand::c052>().NoReply()
		.Command<SnippetCommand::c053>().NoReply()
		.Command<SnippetCommand::c054>().NoReply()
		.Command<SnippetCommand::c055>().NoReply()
		.Command<SnippetCommand::c056>().NoReply()
		.Command<SnippetCommand::c057>().NoReply()
		.Command<SnippetCommand::c058>().NoReply()
		.Command<SnippetCommand::c059>().NoReply()
		.Command<SnippetCommand::c060>().NoReply()
		.Command<SnippetCommand::c061>().NoReply()
		.Command<SnippetCommand::c062>().NoReply()
		.Command<SnippetCommand::c063>().NoReply()
		.Command<SnippetCommand::c064>().NoReply()
		.Command<SnippetCommand::c065>().NoReply()
		.Command<SnippetCommand::c066>().NoReply()
		.Command<SnippetCommand::c067>().NoReply()
		.Command<SnippetCommand::c068>().NoReply()
		.Command<SnippetCommand::c069>().NoReply()
		.Command<SnippetCommand::c070>().NoReply()
		.Command<SnippetCommand::c071>().NoReply()
		.Command<SnippetCommand::c072>().NoReply()
		.Command<SnippetCommand::c073>().NoReply()
		.Command<SnippetCommand::c074>().NoReply()
		.Command<SnippetCommand::c075>().NoReply()
		.Command<SnippetCommand::c076>().NoReply()
		.Command<SnippetCommand::c077>().NoReply()
		.Command<SnippetCommand::c078>().NoReply()
		.Command<SnippetCommand::c079>().NoReply()
		.Command<SnippetCommand::c080>().NoReply()
		.Command<SnippetCommand::c081>().NoReply()
		.Command<SnippetCommand::c082>().NoReply()
		.Command<SnippetCommand::c083>().NoReply()
		.Command<SnippetCommand::c084>().NoReply()
		.Command<SnippetCommand::c085>().NoReply()
		.Command<SnippetCommand::c086>().NoReply()
		.Command<SnippetCommand::c087>().NoReply()
		.Command<SnippetCommand::c088>().NoReply()
		.Command<SnippetCommand::c089>().NoReply()
		.Command<SnippetCommand::c090>().NoReply()
		.Command<SnippetCommand::c091>().NoReply()
		.Command<SnippetCommand::c092>().NoReply()
		.Command<SnippetCommand::c093>().NoReply()
		.Command<SnippetCommand::c094>().NoReply()
		.Command<SnippetCommand::c095>().NoReply()
		.Command<SnippetCommand::c096>().NoReply()
		.Command<SnippetCommand::c097>().NoReply()
		.Command<SnippetCommand::c098>().NoReply()
		.Command<SnippetCommand::c099>().NoReply()
		.Command<SnippetCommand::c100>().NoReply()
		.Command<SnippetCommand::c101>().NoReply()
		.Command<SnippetCommand::c102>().NoReply()
		.Command<SnippetCommand::c103>().NoReply()
		.Command<SnippetCommand::c104>().NoReply()
		.Command<SnippetCommand::c105>().NoReply()
		.Command<SnippetCommand::c106>().NoReply()
		.Command<SnippetCommand::c107>().NoReply()
		.Command<SnippetCommand::c108>().NoReply()
		.Command<SnippetCommand::c109>().NoReply()
		.Command<SnippetCommand::c110>().NoReply()
		.Command<SnippetCommand::c111>().NoReply()
		.Command<SnippetCommand::c112>().NoReply()
		.Command<SnippetCommand::c113>().NoReply()
		.Command<SnippetCommand::c114>().NoReply()
		.Command<SnippetCommand::c115>().NoReply()
		.Command<SnippetCommand::c116>().NoReply()
		.Command<SnippetCommand::c117>().NoReply()
		.Command<SnippetCommand::c118>().NoReply()
		.Command<SnippetCommand::c119>().NoReply()
		.Command<SnippetCommand::c120>().NoReply()
		.Command<SnippetCommand::c121>().NoReply()
		.Command<SnippetCommand::c122>().NoReply()
		.Command<SnippetCommand::c123>().NoReply()
		.Command<SnippetCommand::c124>().NoReply()
		.Command<SnippetCommand::c125>().NoReply()
		.Command<SnippetCommand::c126>().NoReply()
		.Command<SnippetCommand::c127>().NoReply()
		.Command<SnippetCommand::c128>().NoReply()
		.Command<SnippetCommand::c129>().NoReply()
		.Command<SnippetCommand::c130>().NoReply()
		.Command<SnippetCommand::c131>().NoReply()
		.Command<SnippetCommand::c132>().NoReply()
		.Command<SnippetCommand::c133>().NoReply()
		.Command<SnippetCommand::c134>().NoReply()
		.Command<SnippetCommand::c135>().NoReply()
		.Command<SnippetCommand::c136>().NoReply()
		.Command<SnippetCommand::c137>().NoReply()
		.Command<SnippetCommand::c138>().NoReply()
		.Command<SnippetCommand::c139>().NoReply()
		.Command<SnippetCommand::c140>().NoReply()
		.Command<SnippetCommand::c141>().NoReply()
		.Command<SnippetCommand::c142>().NoReply()
		.Command<SnippetCommand::c143>().NoReply()
		.Command<SnippetCommand::c144>().NoReply()
		.Command<SnippetCommand::c145>().NoReply()
		.Command<SnippetCommand::c146>().NoReply()
		.Command<SnippetCommand::c147>().NoReply()
		.Command<SnippetCommand::c148>().NoReply()
		.Command<SnippetCommand::c149>().NoReply()
		.Command<SnippetCommand::c150>().NoReply()
		.Command<SnippetCommand::c151>().NoReply()
		.Command<SnippetCommand::c152>().NoReply()
		.Command<SnippetCommand::c153>().NoReply()
		.Command<SnippetCommand::c154>().NoReply()
		.Command<SnippetCommand::c155>().NoReply()
		.Command<SnippetCommand::c156>().NoReply()
		.Command<SnippetCommand::c157>().NoReply()
		.Command<SnippetCommand::c158>().NoReply()
		.Command<SnippetCommand::c159>().NoReply()
		.Command<SnippetCommand::c160>().NoReply()
		.Command<SnippetCommand::c161>().NoReply()
		.Command<SnippetCommand::c162>().NoReply()
		.Command<SnippetCommand::c163>().NoReply()
		.Command<SnippetCommand::c164>().NoReply()
		.Command<SnippetCommand::c165>().NoReply()
		.Command<SnippetCommand::c166>().NoReply()
		.Command<SnippetCommand::c167>().NoReply()
		.Command<SnippetCommand::c168>().NoReply()
		.Command<SnippetCommand::c169>().NoReply()
		.Command<SnippetCommand::c170>().NoReply()
		.Command<SnippetCommand::c171>().NoReply()
		.Command<SnippetCommand::c172>().NoReply()
		.Command<SnippetCommand::c173>().NoReply()
		.Command<SnippetCommand::c174>().NoReply()
		.Command<SnippetCommand::c175>().NoReply()
		.Command<SnippetCommand::c176>().NoReply()
		.Command<SnippetCommand::c177>().NoReply()
		.Command<SnippetCommand::c178>().NoReply()
		.Command<SnippetCommand::c179>().NoReply()
		.Command<SnippetCommand::c180>().NoReply()
		.Command<SnippetCommand::c181>().NoReply()
		.Command<SnippetCommand::c182>().NoReply()
		.Command<SnippetCommand::c183>().NoReply()
		.Command<SnippetCommand::c184>().NoReply()
		.Command<SnippetCommand::c185>().NoReply()
		.Command<SnippetCommand::c186>().NoReply()
		.Command<SnippetCommand::c187>().NoReply()
		.Command<SnippetCommand::c188>().NoReply()
		.Command<SnippetCommand::c189>().NoReply()
		.Command<SnippetCommand::c190>().NoReply()
		.Command<SnippetCommand::c191>().NoReply()
		.Command<SnippetCommand::c192>().NoReply()
		.Command<SnippetCommand::c193>().NoReply()
		.Command<SnippetCommand::c194>().NoReply()
		.Command<SnippetCommand::c195>().NoReply()
		.Command<SnippetCommand::c196>().NoReply()
		.Command<SnippetCommand::c197>().NoReply()
		.Command<SnippetCommand::c198>().NoReply()
		.Command<SnippetCommand::c199>().NoReply()
		.Command<SnippetCommand::c200>().NoReply()
		.Command<SnippetCommand::c201>().NoReply()
		.Command<SnippetCommand::c202>().NoReply()
		.Command<SnippetCommand::c203>().NoReply()
		.Command<SnippetCommand::c204>().NoReply()
		.Command<SnippetCommand::c205>().NoReply()
		.Command<SnippetCommand::c206>().NoReply()
		.Command<SnippetCommand::c207>().NoReply()
		.Command<SnippetCommand::c208>().NoReply()
		.Command<SnippetCommand::c209>().NoReply()
		.Command<SnippetCommand::c210>().NoReply()
		.Command<SnippetCommand::c211>().NoReply()
		.Command<SnippetCommand::c212>().NoReply()
		.Command<SnippetCommand::c213>().NoReply()
		.Command<SnippetCommand::c214>().NoReply()
		.Command<SnippetCommand::c215>().NoReply()
		.Command<SnippetCommand::c216>().NoReply()
		.Command<SnippetCommand::c217>().NoReply()
		.Command<SnippetCommand::c218>().NoReply()
		.Command<SnippetCommand::c219>().NoReply()
		.Command<SnippetCommand::c220>().NoReply()
		.Command<SnippetCommand::c221>().NoReply()
		.Command<SnippetCommand::c222>().NoReply()
		.Command<SnippetCommand::c223>().NoReply()
		.Command<SnippetCommand::c224>().NoReply()
		.Command<SnippetCommand::c225>().NoReply()
		.Command<SnippetCommand::c226>().NoReply()
		.Command<SnippetCommand::c227>().NoReply()
		.Command<SnippetCommand::c228>().NoReply()
		.Command<SnippetCommand::c229>().NoReply()
		.Command<SnippetCommand::c230>().NoReply()
		.Command<SnippetCommand::c231>().NoReply()
		.Command<SnippetCommand::c232>().NoReply()
		.Command<SnippetCommand::c233>().NoReply()
		.Command<SnippetCommand::c234>().NoReply()
		.Command<SnippetCommand::c235>().NoReply()
		.Command<SnippetCommand::c236>().NoReply()
		.Command<SnippetCommand::c237>().NoReply()
		.Command<SnippetCommand::c238>().NoReply()
		.Command<SnippetCommand::c239>().NoReply()
		.Command<SnippetCommand::c240>().NoReply();
}>;


static_assert(Set::commandCount == 241);
