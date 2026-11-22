// expect: a register value is an integer of 2, 4 or 8 bytes, or an array of 16-bit integers
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <array>


std::array<bool, 4> flags{};


using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&flags));
}>;


static_assert(Table::checked);


int main() {
	return 0;
}
