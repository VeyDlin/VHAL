// A control for the object check: a run-time float multiply must reference the soft-float helper on Cortex-M0+. If this
// file stops referencing it, the check no longer sees soft float and the other samples prove nothing.


volatile float sourceValue = 1.5f;
volatile float resultValue = 0.0f;


int main() {
	float product = sourceValue * sourceValue;
	resultValue = product;
	return 0;
}
