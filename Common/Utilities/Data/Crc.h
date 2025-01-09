#pragma once
#include <System/System.h>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>


class CRC {
public:
	template<typename CRCType, uint16 CRCWidth>
	struct Table;

	template<typename CRCType, uint16 CRCWidth>
	struct Parameters {
		CRCType polynomial;   	///< CRC polynomial
		CRCType initialValue; 	///< Initial CRC value
		CRCType finalXOR;     	///< Value to XOR with the final CRC
		bool reflectInput;    	///< true to reflect all input bytes
		bool reflectOutput; 	///< true to reflect the output CRC (reflection occurs before the final XOR)

		inline Table<CRCType, CRCWidth> MakeTable() const {
			return Table<CRCType, CRCWidth>(*this);
		}
	};



	template<typename CRCType, uint16 CRCWidth>
	struct Table {
		inline Table(const Parameters<CRCType, CRCWidth>& params) : parameters(params) {
			InitTable();
		}

		inline Table(Parameters<CRCType, CRCWidth>&& params) : parameters(::std::move(params)) {
			InitTable();
		}

		inline const Parameters<CRCType, CRCWidth>& GetParameters() const {
			return parameters;
		}

		inline const CRCType* GetTable() const {
			return table;
		}

		inline CRCType operator[](unsigned char index) const {
			return table[index];
		}

	private:
		inline void InitTable() {
			static constexpr CRCType BIT_MASK((CRCType(1) << (CRCWidth - CRCType(1))) | ((CRCType(1) << (CRCWidth - CRCType(1))) - CRCType(1)));
			static constexpr CRCType SHIFT((CHAR_BIT >= CRCWidth) ? static_cast<CRCType>(CHAR_BIT - CRCWidth) : 0);

			CRCType crc;
			unsigned char byte = 0;
			do {
				crc = CalculateRemainder<CRCType, CRCWidth>(&byte, sizeof(byte), parameters, CRCType(0));
				crc &= BIT_MASK;

				if (!parameters.reflectInput && CRCWidth < CHAR_BIT) {
					crc = static_cast<CRCType>(crc << SHIFT);
				}

				table[byte] = crc;
			} while (++byte);
		}

		Parameters<CRCType, CRCWidth> parameters; ///< CRC parameters used to construct the table
		CRCType table[1 << CHAR_BIT];             ///< CRC lookup table
	};




	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType Calculate(const void* data, size_t size, const Parameters<CRCType, CRCWidth>& parameters) {
		CRCType remainder = CalculateRemainder(data, size, parameters, parameters.initialValue);
		return Finalize<CRCType, CRCWidth>(remainder, parameters.finalXOR, parameters.reflectInput != parameters.reflectOutput);
	}





	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType Calculate(const void* data, size_t size, const Parameters<CRCType, CRCWidth>& parameters, CRCType crc) {
		CRCType remainder = UndoFinalize<CRCType, CRCWidth>(crc, parameters.finalXOR, parameters.reflectInput != parameters.reflectOutput);
		remainder = CalculateRemainder(data, size, parameters, remainder);
		return Finalize<CRCType, CRCWidth>(remainder, parameters.finalXOR, parameters.reflectInput != parameters.reflectOutput);
	}





	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType Calculate(const void* data, size_t size, const Table<CRCType, CRCWidth>& lookupTable) {
		const Parameters<CRCType, CRCWidth>& parameters = lookupTable.GetParameters();
		CRCType remainder = CalculateRemainder(data, size, lookupTable, parameters.initialValue);
		return Finalize<CRCType, CRCWidth>(remainder, parameters.finalXOR, parameters.reflectInput != parameters.reflectOutput);
	}





	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType Calculate(const void* data, size_t size, const Table<CRCType, CRCWidth>& lookupTable, CRCType crc) {
		const Parameters<CRCType, CRCWidth>& parameters = lookupTable.GetParameters();
		CRCType remainder = UndoFinalize<CRCType, CRCWidth>(crc, parameters.finalXOR, parameters.reflectInput != parameters.reflectOutput);
		remainder = CalculateRemainder(data, size, lookupTable, remainder);
		return Finalize<CRCType, CRCWidth>(remainder, parameters.finalXOR, parameters.reflectInput != parameters.reflectOutput);
	}




    static inline const Parameters<uint8, 4>& CRC_4_ITU() {
        static const Parameters<uint8, 4> parameters = { 0x3, 0x0, 0x0, true, true };
        return parameters;
    }


    static inline const Parameters<uint8, 5>& CRC_5_EPC() {
        static const Parameters<uint8, 5> parameters = { 0x09, 0x09, 0x00, false, false };
        return parameters;
    }


    static inline const Parameters<uint8, 5>& CRC_5_ITU() {
        static const Parameters<uint8, 5> parameters = { 0x15, 0x00, 0x00, true, true };
        return parameters;
    }


    static inline const Parameters<uint8, 5>& CRC_5_USB() {
        static const Parameters<uint8, 5> parameters = { 0x05, 0x1F, 0x1F, true, true };
        return parameters;
    }


    static inline const Parameters<uint8, 6>& CRC_6_CDMA2000A() {
        static const Parameters<uint8, 6> parameters = { 0x27, 0x3F, 0x00, false, false };
        return parameters;
    }


    static inline const Parameters<uint8, 6>& CRC_6_CDMA2000B() {
        static const Parameters<uint8, 6> parameters = { 0x07, 0x3F, 0x00, false, false };
        return parameters;
    }


    static inline const Parameters<uint8, 6>& CRC_6_ITU() {
        static const Parameters<uint8, 6> parameters = { 0x03, 0x00, 0x00, true, true };
        return parameters;
    }


    static inline const Parameters<uint8, 7>& CRC_7() {
        static const Parameters<uint8, 7> parameters = { 0x09, 0x00, 0x00, false, false };
        return parameters;
    }


    static inline const Parameters<uint8, 8>& CRC_8() {
        static const Parameters<uint8, 8> parameters = { 0x07, 0x00, 0x00, false, false };
        return parameters;
    }


    static inline const Parameters<uint8, 8>& CRC_8_EBU() {
        static const Parameters<uint8, 8> parameters = { 0x1D, 0xFF, 0x00, true, true };
        return parameters;
    }


    static inline const Parameters<uint8, 8>& CRC_8_MAXIM() {
        static const Parameters<uint8, 8> parameters = { 0x31, 0x00, 0x00, true, true };
        return parameters;
    }


    static inline const Parameters<uint8, 8>& CRC_8_WCDMA() {
        static const Parameters<uint8, 8> parameters = { 0x9B, 0x00, 0x00, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 10>& CRC_10() {
        static const Parameters<uint16, 10> parameters = { 0x233, 0x000, 0x000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 10>& CRC_10_CDMA2000() {
        static const Parameters<uint16, 10> parameters = { 0x3D9, 0x3FF, 0x000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 11>& CRC_11() {
        static const Parameters<uint16, 11> parameters = { 0x385, 0x01A, 0x000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 12>& CRC_12_CDMA2000() {
        static const Parameters<uint16, 12> parameters = { 0xF13, 0xFFF, 0x000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 12>& CRC_12_DECT() {
        static const Parameters<uint16, 12> parameters = { 0x80F, 0x000, 0x000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 12>& CRC_12_UMTS() {
        static const Parameters<uint16, 12> parameters = { 0x80F, 0x000, 0x000, false, true };
        return parameters;
    }


    static inline const Parameters<uint16, 13>& CRC_13_BBC() {
        static const Parameters<uint16, 13> parameters = { 0x1CF5, 0x0000, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 15>& CRC_15() {
        static const Parameters<uint16, 15> parameters = { 0x4599, 0x0000, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 15>& CRC_15_MPT1327() {
        static const Parameters<uint16, 15> parameters = { 0x6815, 0x0000, 0x0001, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_ARC() {
        static const Parameters<uint16, 16> parameters = { 0x8005, 0x0000, 0x0000, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_BUYPASS() {
        static const Parameters<uint16, 16> parameters = { 0x8005, 0x0000, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_CCITTFALSE() {
        static const Parameters<uint16, 16> parameters = { 0x1021, 0xFFFF, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_CDMA2000() {
        static const Parameters<uint16, 16> parameters = { 0xC867, 0xFFFF, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_CMS() {
        static const Parameters<uint16, 16> parameters = { 0x8005, 0xFFFF, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_DECTR() {
        static const Parameters<uint16, 16> parameters = { 0x0589, 0x0000, 0x0001, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_DECTX() {
        static const Parameters<uint16, 16> parameters = { 0x0589, 0x0000, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_DNP() {
        static const Parameters<uint16, 16> parameters = { 0x3D65, 0x0000, 0xFFFF, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_GENIBUS() {
        static const Parameters<uint16, 16> parameters = { 0x1021, 0xFFFF, 0xFFFF, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_KERMIT() {
        static const Parameters<uint16, 16> parameters = { 0x1021, 0x0000, 0x0000, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_MAXIM() {
        static const Parameters<uint16, 16> parameters = { 0x8005, 0x0000, 0xFFFF, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_MODBUS() {
        static const Parameters<uint16, 16> parameters = { 0x8005, 0xFFFF, 0x0000, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_T10DIF() {
        static const Parameters<uint16, 16> parameters = { 0x8BB7, 0x0000, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_USB() {
        static const Parameters<uint16, 16> parameters = { 0x8005, 0xFFFF, 0xFFFF, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_X25() {
        static const Parameters<uint16, 16> parameters = { 0x1021, 0xFFFF, 0xFFFF, true, true };
        return parameters;
    }


    static inline const Parameters<uint16, 16>& CRC_16_XMODEM() {
        static const Parameters<uint16, 16> parameters = { 0x1021, 0x0000, 0x0000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 17>& CRC_17_CAN() {
        static const Parameters<uint32, 17> parameters = { 0x1685B, 0x00000, 0x00000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 21>& CRC_21_CAN() {
        static const Parameters<uint32, 21> parameters = { 0x102899, 0x000000, 0x000000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 24>& CRC_24() {
        static const Parameters<uint32, 24> parameters = { 0x864CFB, 0xB704CE, 0x000000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 24>& CRC_24_FLEXRAYA() {
        static const Parameters<uint32, 24> parameters = { 0x5D6DCB, 0xFEDCBA, 0x000000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 24>& CRC_24_FLEXRAYB() {
        static const Parameters<uint32, 24> parameters = { 0x5D6DCB, 0xABCDEF, 0x000000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 30>& CRC_30() {
        static const Parameters<uint32, 30> parameters = { 0x2030B9C7, 0x3FFFFFFF, 0x00000000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 32>& CRC_32() {
        static const Parameters<uint32, 32> parameters = { 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, true };
        return parameters;
    }


    static inline const Parameters<uint32, 32>& CRC_32_BZIP2() {
        static const Parameters<uint32, 32> parameters = { 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 32>& CRC_32_C() {
        static const Parameters<uint32, 32> parameters = { 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true };
        return parameters;
    }


    static inline const Parameters<uint32, 32>& CRC_32_MPEG2() {
        static const Parameters<uint32, 32> parameters = { 0x04C11DB7, 0xFFFFFFFF, 0x00000000, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 32>& CRC_32_POSIX() {
        static const Parameters<uint32, 32> parameters = { 0x04C11DB7, 0x00000000, 0xFFFFFFFF, false, false };
        return parameters;
    }


    static inline const Parameters<uint32, 32>& CRC_32_Q() {
        static const Parameters<uint32, 32> parameters = { 0x814141AB, 0x00000000, 0x00000000, false, false };
        return parameters;
    }


    static inline const Parameters<uint64, 40>& CRC_40_GSM() {
        static const Parameters<uint64, 40> parameters = { 0x0004820009, 0x0000000000, 0xFFFFFFFFFF, false, false };
        return parameters;
    }


    static inline const Parameters<uint64, 64>& CRC_64() {
        static const Parameters<uint64, 64> parameters = { 0x42F0E1EBA9EA3693, 0x0000000000000000, 0x0000000000000000, false, false };
        return parameters;
    }


private:
	template<typename IntegerType>
	static inline IntegerType Reflect(IntegerType value, uint16 numBits) {
		IntegerType reversedValue(0);

		for (uint16 i = 0; i < numBits; ++i) {
			reversedValue = static_cast<IntegerType>((reversedValue << 1) | (value & 1));
			value = static_cast<IntegerType>(value >> 1);
		}

		return reversedValue;
	}





	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType Finalize(CRCType remainder, CRCType finalXOR, bool reflectOutput) {
		static constexpr CRCType BIT_MASK = (CRCType(1) << (CRCWidth - CRCType(1))) | ((CRCType(1) << (CRCWidth - CRCType(1))) - CRCType(1));

		if (reflectOutput) {
			remainder = Reflect(remainder, CRCWidth);
		}

		return (remainder ^ finalXOR) & BIT_MASK;
	}





	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType UndoFinalize(CRCType crc, CRCType finalXOR, bool reflectOutput) {
		static constexpr CRCType BIT_MASK = (CRCType(1) << (CRCWidth - CRCType(1))) | ((CRCType(1) << (CRCWidth - CRCType(1))) - CRCType(1));
		crc = (crc & BIT_MASK) ^ finalXOR;

		if (reflectOutput) {
			crc = Reflect(crc, CRCWidth);
		}

		return crc;
	}





	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType CalculateRemainder(const void* data, size_t size, const Parameters<CRCType, CRCWidth>& parameters, CRCType remainder) {
		static_assert(::std::numeric_limits<CRCType>::digits >= CRCWidth, "CRCType is too small to contain a CRC of width CRCWidth.");
		const unsigned char* current = reinterpret_cast<const unsigned char*>(data);

		if (parameters.reflectInput) {
			CRCType polynomial = CRC::Reflect(parameters.polynomial, CRCWidth);
			while (size--) {
				remainder = static_cast<CRCType>(remainder ^ *current++);

				for (size_t i = 0; i < CHAR_BIT; ++i) {
					remainder = static_cast<CRCType>((remainder >> 1) ^ ((remainder & 1) * polynomial));
				}
			}
		} else if (CRCWidth >= CHAR_BIT) {
			static constexpr CRCType CRC_WIDTH_MINUS_ONE(CRCWidth - CRCType(1));
			static constexpr CRCType SHIFT((CRCWidth >= CHAR_BIT) ? static_cast<CRCType>(CRCWidth - CHAR_BIT) : 0);

			while (size--) {
				remainder = static_cast<CRCType>(remainder ^ (static_cast<CRCType>(*current++) << SHIFT));

				for (size_t i = 0; i < CHAR_BIT; ++i) {
					remainder = static_cast<CRCType>((remainder << 1) ^ (((remainder >> CRC_WIDTH_MINUS_ONE) & 1) * parameters.polynomial));
				}
			}
		} else {
			static constexpr CRCType CHAR_BIT_MINUS_ONE(CHAR_BIT - 1);
			static constexpr CRCType SHIFT((CHAR_BIT >= CRCWidth) ? static_cast<CRCType>(CHAR_BIT - CRCWidth) : 0);

			CRCType polynomial = static_cast<CRCType>(parameters.polynomial << SHIFT);
			remainder = static_cast<CRCType>(remainder << SHIFT);

			while (size--) {
				remainder = static_cast<CRCType>(remainder ^ *current++);

				for (size_t i = 0; i < CHAR_BIT; ++i) {
					remainder = static_cast<CRCType>((remainder << 1) ^ (((remainder >> CHAR_BIT_MINUS_ONE) & 1) * polynomial));
				}
			}

			remainder = static_cast<CRCType>(remainder >> SHIFT);
		}

		return remainder;
	}





	template<typename CRCType, uint16 CRCWidth>
	static inline CRCType CalculateRemainder(const void* data, size_t size, const Table<CRCType, CRCWidth>& lookupTable, CRCType remainder) {
		const unsigned char* current = reinterpret_cast<const unsigned char*>(data);

		if (lookupTable.GetParameters().reflectInput) {
			while (size--) {
				remainder = static_cast<CRCType>((remainder >> CHAR_BIT) ^ lookupTable[static_cast<unsigned char>(remainder ^ *current++)]);
			}
		} else if (CRCWidth >= CHAR_BIT) {
			static constexpr CRCType SHIFT((CRCWidth >= CHAR_BIT) ? static_cast<CRCType>(CRCWidth - CHAR_BIT) : 0);

			while (size--) {
				remainder = static_cast<CRCType>((remainder << CHAR_BIT) ^ lookupTable[static_cast<unsigned char>((remainder >> SHIFT) ^ *current++)]);
			}
		} else {
			static constexpr CRCType SHIFT((CHAR_BIT >= CRCWidth) ? static_cast<CRCType>(CHAR_BIT - CRCWidth) : 0);

			remainder = static_cast<CRCType>(remainder << SHIFT);
			while (size--) {
				remainder = lookupTable[static_cast<unsigned char>(remainder ^ *current++)];
			}

			remainder = static_cast<CRCType>(remainder >> SHIFT);
		}

		return remainder;
	}
};