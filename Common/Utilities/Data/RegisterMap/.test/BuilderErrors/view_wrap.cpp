// expect: a block runs past Modbus address 0xFFFF
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <array>


std::array<uint16, 2> pair{};


using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0xFFFF, Variable(&pair));
}>;


static_assert(Table::checked);


int main() {
	return 0;
}
