// expect: the read bit must not be one of the address bits
#include <Drivers/Interface/RegisterBus/SpiRegisterBus.h>


struct FakeSpi {
	void Continuous(bool) {
	}


	template<typename DataType>
	ResultStatus WriteArray(const DataType*, uint32) {
		return ResultStatus::ok;
	}


	template<typename DataType>
	ResultStatus ReadArray(DataType*, uint32) {
		return ResultStatus::ok;
	}
};


using Bus = SpiRegisterBus<FakeSpi, +[] {
	return SpiFramingBuilder().ReadBit(3);
}>;


int main() {
	FakeSpi spi;
	Bus bus(spi);
	return 0;
}
