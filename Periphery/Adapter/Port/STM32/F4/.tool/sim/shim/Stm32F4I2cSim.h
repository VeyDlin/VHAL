#pragma once
// Register model of the F4 I2C peripheral for the host tests. It has the CMSIS bit names and the LL functions that
// I2CAdapterF4.h reaches, and nothing else. The test drives the bus by hand: it sets the status flags before each
// interrupt, and it reads back what the adapter wrote to DR and CR1. The model sees every write, whether it comes through
// an LL call or a direct register access: a rising START bit starts a transfer, and a DR write after it is the address
// byte, which clears START as the hardware does. In auto mode, START raises SB and the address byte acknowledges or refuses
// the device through ADDR or AF, so the blocking CheckDevice and Scan run unchanged.
#include <System/System.h>
#include <deque>
#include <vector>


// CR1 bits.
#define I2C_CR1_PE       (1U << 0)
#define I2C_CR1_START    (1U << 8)
#define I2C_CR1_STOP     (1U << 9)
#define I2C_CR1_ACK      (1U << 10)
#define I2C_CR1_POS      (1U << 11)

// CR2 interrupt enables.
#define I2C_CR2_ITERREN  (1U << 8)
#define I2C_CR2_ITEVTEN  (1U << 9)
#define I2C_CR2_ITBUFEN  (1U << 10)

// SR1 status bits and their positions.
#define I2C_SR1_SB       (1U << 0)
#define I2C_SR1_ADDR     (1U << 1)
#define I2C_SR1_BTF      (1U << 2)
#define I2C_SR1_STOPF    (1U << 4)
#define I2C_SR1_RXNE     (1U << 6)
#define I2C_SR1_TXE      (1U << 7)
#define I2C_SR1_BERR     (1U << 8)
#define I2C_SR1_ARLO     (1U << 9)
#define I2C_SR1_AF       (1U << 10)
#define I2C_SR1_OVR      (1U << 11)

#define I2C_SR1_SB_Pos    0U
#define I2C_SR1_ADDR_Pos  1U
#define I2C_SR1_BTF_Pos   2U
#define I2C_SR1_STOPF_Pos 4U
#define I2C_SR1_RXNE_Pos  6U
#define I2C_SR1_TXE_Pos   7U
#define I2C_SR1_AF_Pos    10U

// SR2 status bits.
#define I2C_SR2_BUSY     (1U << 1)
#define I2C_SR2_BUSY_Pos 1U


// HAL flag status, as in stm32f4xx_hal_def.h.
typedef enum {
	RESET = 0,
	SET = 1
} FlagStatus;


// LL constants.
#define LL_I2C_MODE_I2C          0x00000000U
#define LL_I2C_DUTYCYCLE_2       0x00000000U
#define LL_I2C_DUTYCYCLE_16_9    0x00000001U
#define LL_I2C_ACK               0x00000000U
#define LL_I2C_NACK              0x00000001U
#define LL_I2C_OWNADDRESS1_7BIT  0x00000000U


struct I2C_TypeDef;


// Model state. The test resets it before each case.
inline std::vector<uint32> simWritten;
inline std::deque<uint8> simRxQueue;
inline bool simAutoAck = false;
inline std::vector<uint32> simAckDevices;
inline bool simAddressPending = false;
inline uint32 simStartCount = 0;
inline uint32 simStopCount = 0;
inline uint32 simInitClockSpeed = 0;


inline void SimResetModel() {
	simWritten.clear();
	simRxQueue.clear();
	simAutoAck = false;
	simAckDevices.clear();
	simAddressPending = false;
	simStartCount = 0;
	simStopCount = 0;
	simInitClockSpeed = 0;
}


// The control register. A rising START bit starts a transfer and a rising STOP bit ends one. Writes go through the proxy so
// that the model sees the edge whether the adapter uses an LL call or a direct access.
struct SimControlRegister {
	I2C_TypeDef *owner = nullptr;
	uint32 value = 0;

	SimControlRegister& operator=(uint32 newValue);
	SimControlRegister& operator|=(uint32 bits);
	SimControlRegister& operator&=(uint32 bits);
	operator uint32() const {
		return value;
	}
};


// The data register. A write records the byte, and the byte after a START is the address: it acknowledges or refuses the
// device when auto mode is on. A read returns the next queued byte, or 0xEE when none is queued.
struct SimDataRegister {
	I2C_TypeDef *owner = nullptr;

	SimDataRegister& operator=(uint32 value);
	operator uint32() const;
};


struct I2C_TypeDef {
	SimControlRegister CR1;
	uint32 CR2 = 0;
	uint32 OAR1 = 0;
	uint32 OAR2 = 0;
	SimDataRegister DR;
	uint32 SR1 = 0;
	uint32 SR2 = 0;
	uint32 CCR = 0;
	uint32 TRISE = 0;
	uint32 FLTR = 0;


	I2C_TypeDef() {
		CR1.owner = this;
		DR.owner = this;
	}
};


struct LL_I2C_InitTypeDef {
	uint32 PeripheralMode = 0;
	uint32 ClockSpeed = 0;
	uint32 DutyCycle = 0;
	uint32 OwnAddress1 = 0;
	uint32 TypeAcknowledge = 0;
	uint32 OwnAddrSize = 0;
};


inline void SimStartTransfer(I2C_TypeDef *i2cx) {
	simStartCount++;
	simAddressPending = true;
	i2cx->CR1.value &= ~I2C_CR1_STOP;
	i2cx->SR1 = simAutoAck ? I2C_SR1_SB : 0;
}


inline SimControlRegister& SimControlRegister::operator=(uint32 newValue) {
	bool startRises = (newValue & I2C_CR1_START) != 0 && (value & I2C_CR1_START) == 0;
	bool stopRises = (newValue & I2C_CR1_STOP) != 0 && (value & I2C_CR1_STOP) == 0;

	value = newValue;
	if (stopRises) {
		simStopCount++;
	}
	if (startRises) {
		SimStartTransfer(owner);
	}
	return *this;
}


inline SimControlRegister& SimControlRegister::operator|=(uint32 bits) {
	return *this = (value | bits);
}


inline SimControlRegister& SimControlRegister::operator&=(uint32 bits) {
	value &= bits;
	return *this;
}


inline SimDataRegister& SimDataRegister::operator=(uint32 value) {
	simWritten.push_back(value);

	if (simAddressPending) {
		simAddressPending = false;
		owner->CR1.value &= ~I2C_CR1_START;

		if (simAutoAck) {
			bool acknowledged = false;
			for (uint32 device : simAckDevices) {
				if (device == (value >> 1)) {
					acknowledged = true;
				}
			}

			owner->SR1 &= ~I2C_SR1_SB;
			if (!acknowledged) {
				owner->SR1 |= I2C_SR1_AF;
			} else {
				// An acknowledged write address leaves the data register empty (TXE). An acknowledged read address has
				// the first byte of the queue waiting (RXNE).
				owner->SR1 |= I2C_SR1_ADDR;
				if ((value & 1U) == 0) {
					owner->SR1 |= I2C_SR1_TXE;
				} else if (!simRxQueue.empty()) {
					owner->SR1 |= I2C_SR1_RXNE;
				}
			}
		}
	} else {
		// A data byte has left the shift register once the next one is written, so BTF follows every data write.
		owner->SR1 |= I2C_SR1_BTF;
	}
	return *this;
}


inline SimDataRegister::operator uint32() const {
	if (simRxQueue.empty()) {
		return 0xEE;
	}

	uint8 value = simRxQueue.front();
	simRxQueue.pop_front();
	return value;
}


inline uint32 LL_I2C_IsEnabled(I2C_TypeDef *i2cx) {
	return (i2cx->CR1 & I2C_CR1_PE) != 0;
}


inline void LL_I2C_Enable(I2C_TypeDef *i2cx) {
	i2cx->CR1 |= I2C_CR1_PE;
}


inline void LL_I2C_Init(I2C_TypeDef *, LL_I2C_InitTypeDef *init) {
	simInitClockSpeed = init->ClockSpeed;
}


inline void LL_I2C_SetOwnAddress2(I2C_TypeDef *, uint32) { }
inline void LL_I2C_DisableOwnAddress2(I2C_TypeDef *) { }
inline void LL_I2C_DisableGeneralCall(I2C_TypeDef *) { }
inline void LL_I2C_EnableClockStretching(I2C_TypeDef *) { }
inline void LL_I2C_DisableClockStretching(I2C_TypeDef *) { }


inline void LL_I2C_AcknowledgeNextData(I2C_TypeDef *i2cx, uint32 ack) {
	if (ack == LL_I2C_ACK) {
		i2cx->CR1 |= I2C_CR1_ACK;
	} else {
		i2cx->CR1 &= ~I2C_CR1_ACK;
	}
}


inline void LL_I2C_EnableBitPOS(I2C_TypeDef *i2cx) {
	i2cx->CR1 |= I2C_CR1_POS;
}


inline void LL_I2C_DisableBitPOS(I2C_TypeDef *i2cx) {
	i2cx->CR1 &= ~I2C_CR1_POS;
}


inline void LL_I2C_GenerateStartCondition(I2C_TypeDef *i2cx) {
	i2cx->CR1 |= I2C_CR1_START;
}


inline void LL_I2C_GenerateStopCondition(I2C_TypeDef *i2cx) {
	i2cx->CR1 |= I2C_CR1_STOP;
}


inline void LL_I2C_EnableIT_EVT(I2C_TypeDef *i2cx) { i2cx->CR2 |= I2C_CR2_ITEVTEN; }
inline void LL_I2C_DisableIT_EVT(I2C_TypeDef *i2cx) { i2cx->CR2 &= ~I2C_CR2_ITEVTEN; }
inline uint32 LL_I2C_IsEnabledIT_EVT(I2C_TypeDef *i2cx) { return (i2cx->CR2 & I2C_CR2_ITEVTEN) != 0; }

inline void LL_I2C_EnableIT_BUF(I2C_TypeDef *i2cx) { i2cx->CR2 |= I2C_CR2_ITBUFEN; }
inline void LL_I2C_DisableIT_BUF(I2C_TypeDef *i2cx) { i2cx->CR2 &= ~I2C_CR2_ITBUFEN; }
inline uint32 LL_I2C_IsEnabledIT_BUF(I2C_TypeDef *i2cx) { return (i2cx->CR2 & I2C_CR2_ITBUFEN) != 0; }

inline void LL_I2C_EnableIT_ERR(I2C_TypeDef *i2cx) { i2cx->CR2 |= I2C_CR2_ITERREN; }
inline void LL_I2C_DisableIT_ERR(I2C_TypeDef *i2cx) { i2cx->CR2 &= ~I2C_CR2_ITERREN; }
inline uint32 LL_I2C_IsEnabledIT_ERR(I2C_TypeDef *i2cx) { return (i2cx->CR2 & I2C_CR2_ITERREN) != 0; }


inline uint32 LL_I2C_IsActiveFlag_SB(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_SB) != 0; }
inline uint32 LL_I2C_IsActiveFlag_ADDR(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_ADDR) != 0; }
inline uint32 LL_I2C_IsActiveFlag_BTF(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_BTF) != 0; }
inline uint32 LL_I2C_IsActiveFlag_TXE(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_TXE) != 0; }
inline uint32 LL_I2C_IsActiveFlag_RXNE(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_RXNE) != 0; }
inline uint32 LL_I2C_IsActiveFlag_AF(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_AF) != 0; }
inline uint32 LL_I2C_IsActiveFlag_BERR(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_BERR) != 0; }
inline uint32 LL_I2C_IsActiveFlag_ARLO(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_ARLO) != 0; }
inline uint32 LL_I2C_IsActiveFlag_OVR(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_OVR) != 0; }
inline uint32 LL_I2C_IsActiveFlag_STOP(I2C_TypeDef *i2cx) { return (i2cx->SR1 & I2C_SR1_STOPF) != 0; }


inline void LL_I2C_ClearFlag_ADDR(I2C_TypeDef *i2cx) { i2cx->SR1 &= ~I2C_SR1_ADDR; }
inline void LL_I2C_ClearFlag_AF(I2C_TypeDef *i2cx) { i2cx->SR1 &= ~I2C_SR1_AF; }
inline void LL_I2C_ClearFlag_STOP(I2C_TypeDef *i2cx) { i2cx->SR1 &= ~I2C_SR1_STOPF; }
inline void LL_I2C_ClearFlag_BERR(I2C_TypeDef *i2cx) { i2cx->SR1 &= ~I2C_SR1_BERR; }
inline void LL_I2C_ClearFlag_ARLO(I2C_TypeDef *i2cx) { i2cx->SR1 &= ~I2C_SR1_ARLO; }
inline void LL_I2C_ClearFlag_OVR(I2C_TypeDef *i2cx) { i2cx->SR1 &= ~I2C_SR1_OVR; }


inline void LL_I2C_TransmitData8(I2C_TypeDef *i2cx, uint8 data) {
	i2cx->DR = data;
}


inline uint8 LL_I2C_ReceiveData8(I2C_TypeDef *i2cx) {
	return static_cast<uint8>(static_cast<uint32>(i2cx->DR));
}
