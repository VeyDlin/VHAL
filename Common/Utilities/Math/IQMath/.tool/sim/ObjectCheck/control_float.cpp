// A control for the object check: a run-time float multiply must reference the soft-float helper on Cortex-M0+. If this
// file that references no helper would leave the check blind to soft float, and the other samples would prove nothing.


volatile float sourceValue = 1.5f;
volatile float resultValue = 0.0f;


int main() {
	float product = sourceValue * sourceValue;
	resultValue = product;
	return 0;
}
