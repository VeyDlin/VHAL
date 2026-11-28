#include <VHAL.h>
#include <Utilities/Data/Bit.h>


// Sets bit 8 of a uint8. The position is outside the value, so the assertion in Bit::Set stops the program. The script that runs this
// file expects that stop and its message; the program never reaches its return.
int main() {
	uint8 value = 0;
	Bit::Set(value, 8);
	return 0;
}
