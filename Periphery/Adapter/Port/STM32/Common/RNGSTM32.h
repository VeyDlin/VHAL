#pragma once
#include <Adapter/RNGAdapter.h>


// Shared STM32 RNG register code for G0, G4 and F4. A family file derives from RNGAdapterSTM32 and supplies only the
// bus clock check. Everything here is compiled only where the CMSIS device header defines RNG, so a chip without the
// peripheral gets an empty header instead of a compile error.
//
// What the BSP must do (the adapter never touches RCC or NVIC):
//   - Select a 48 MHz kernel clock for the RNG: PLL48CK on F4, HSI48 or PLLQ on G4, HSI48 on G0. A wrong clock is
//     reported as a clock error, never as a weak value.
//   - Enable the RNG bus clock. Initialization() returns notReady while it is off. The family file names the bit.
//   - Leave the RNG interrupt unwired. The port never sets CR.IE, so the line does not fire. If the BSP wires it anyway
//     (RNG_IRQn, or AES_RNG_IRQn on G0), forward it to IrqHandler(); the handler only latches and clears the seed and
//     clock error flags.
//
// Why polling: with CR.IE set the RNG asserts its interrupt on every DRDY and keeps the core in the ISR until someone
// reads DR. Read() therefore polls DRDY against System::GetMs() with Parameters::timeoutMs as the limit.
//
// Error recovery follows the reference manuals and ST's HAL. A seed or clock error stops the output, so the word in DR
// is never returned. Read() clears SEIS/CEIS, disables and enables the RNG (the restart ST's HAL recommends for a seed
// error) and returns invalidData. The next Read() succeeds once the hardware has reseeded. A clock error only clears
// once the clock is fixed; restarting while it persists is harmless.

#if defined(RNG)

class RNGAdapterSTM32: public RNGAdapter<RNG_TypeDef> {
private:
	bool initialized = false;
	volatile bool errorPending = false;


public:
	RNGAdapterSTM32() = default;


	RNGAdapterSTM32(RNG_TypeDef *rng):RNGAdapter(rng) {
	}


	// Fallback for a wired RNG interrupt. Latches the seed or clock error so the next Read() reports it. The flags are
	// cleared here because the line stays asserted while they are set. Not declared in the RNGAdapter contract, so it
	// is not virtual and the BSP must call it on the concrete RNGAdapterXx type.
	inline void IrqHandler() {
		if ((LL_RNG_IsActiveFlag_SEIS(rngHandle) != 0U) || (LL_RNG_IsActiveFlag_CEIS(rngHandle) != 0U)) {
			errorPending = true;
			LL_RNG_ClearFlag_SEIS(rngHandle);
			LL_RNG_ClearFlag_CEIS(rngHandle);
		}
	}


	virtual Result<uint32> Read() override {
		if (!initialized) {
			return ResultStatus::noInit;
		}

		uint64 startMs = System::GetMs();

		while (true) {
			// Checked before DRDY: a word can be ready while a seed error is active, and that word has no entropy.
			if (errorPending || HasSourceError()) {
				RestartSource();
				return ResultStatus::invalidData;
			}

			if (LL_RNG_IsActiveFlag_DRDY(rngHandle) != 0U) {
				return LL_RNG_ReadRandData32(rngHandle);
			}

			if (System::GetMs() - startMs >= parameters.timeoutMs) {
				return ResultStatus::timeout;
			}
		}
	}


protected:
	virtual ResultStatus Initialization() override {
		initialized = false;

		if (rngHandle == nullptr) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!IsClockEnabled()) {
			return ResultStatus::notReady;
		}

		// Clock error detection is on whenever the peripheral has it. The hardware bit is CED = 0 for enabled, and the
		// LL call takes care of that inversion.
		#if defined(RNG_CR_CED)
			LL_RNG_EnableClkErrorDetect(rngHandle);
		#endif

		// Start from clean error latches so a re-initialization does not report an error from before it.
		LL_RNG_ClearFlag_SEIS(rngHandle);
		LL_RNG_ClearFlag_CEIS(rngHandle);
		errorPending = false;
		LL_RNG_Enable(rngHandle);

		status = AfterInitialization();
		initialized = (status == ResultStatus::ok);
		return status;
	}


	// Pure virtual: the RNG enable bit sits on AHB1 on G0 and F410, and on AHB2 on G4 and the other F4 chips, so each
	// family file supplies the check.
	virtual bool IsClockEnabled() const = 0;


private:
	bool HasSourceError() const {
		if (LL_RNG_IsActiveFlag_SEIS(rngHandle) != 0U) {
			return true;
		}

		if (LL_RNG_IsActiveFlag_CEIS(rngHandle) != 0U) {
			return true;
		}

		if (LL_RNG_IsActiveFlag_SECS(rngHandle) != 0U) {
			return true;
		}

		return LL_RNG_IsActiveFlag_CECS(rngHandle) != 0U;
	}


	// Clears the error latches and restarts the source. Disabling RNGEN resets the RNG so it reseeds from the start;
	// clearing the latches first stops the interrupt line from staying asserted.
	void RestartSource() {
		LL_RNG_ClearFlag_SEIS(rngHandle);
		LL_RNG_ClearFlag_CEIS(rngHandle);
		LL_RNG_Disable(rngHandle);
		LL_RNG_Enable(rngHandle);
		errorPending = false;
	}
};

#endif
