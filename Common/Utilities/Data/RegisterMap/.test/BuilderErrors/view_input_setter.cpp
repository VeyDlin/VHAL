// expect: input registers and discrete inputs cannot take a set function
#include <Utilities/Data/RegisterMap/RegisterView.h>


uint16 readValue() {
	return 1;
}


ResultStatus writeValue(uint16) {
	return ResultStatus::ok;
}


using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.InputRegisters(0x0000, Computed(&readValue, &writeValue));
}>;


static_assert(Table::checked);


int main() {
	return 0;
}
