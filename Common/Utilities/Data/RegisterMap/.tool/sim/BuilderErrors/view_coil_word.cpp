// expect: a coil or discrete input is a bool or an array of bool
#include <Utilities/Data/RegisterMap/RegisterView.h>


uint16 word = 0;


using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.Coils(0x0000, Variable(&word));
}>;


static_assert(Table::checked);


int main() {
	return 0;
}
