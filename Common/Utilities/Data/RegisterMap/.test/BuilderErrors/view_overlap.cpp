// expect: two view entries overlap in the same table at one Modbus address
#include <Utilities/Data/RegisterMap/RegisterView.h>


uint16 first = 0;
uint16 second = 0;


using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0010, Variable(&first))
		.HoldingRegisters(0x0010, Variable(&second));
}>;


static_assert(Table::checked);


int main() {
	return 0;
}
