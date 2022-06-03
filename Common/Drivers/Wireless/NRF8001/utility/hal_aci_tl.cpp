#include <PortAdapters.h>
#ifdef AUSED_SPI_ADAPTER

#include <BSP.h>
#include "hal_platform.h"
#include "hal_aci_tl.h"
#include "aci_queue.h"

#ifdef HAL_ACI_TL_DEBUG
static void m_aci_data_print(hal_aci_data_t *p_data);
#endif
static void m_aci_event_check(void);
#ifdef HAL_ACI_TL_INTERRUPT
static void m_aci_isr(void);
#endif
static void m_aci_pins_set(aci_pins_t *a_pins_ptr);
static inline void m_aci_reqn_disable(void);
static inline void m_aci_reqn_enable(void);
static void m_aci_q_flush(void);
static bool m_aci_spi_transfer(hal_aci_data_t *data_to_send,
		hal_aci_data_t *received_data);

static uint8_t spi_readwrite(uint8_t aci_byte);
#ifdef HAL_ACI_TL_DEBUG
static bool aci_debug_print = false;
#endif

aci_queue_t aci_tx_q;
aci_queue_t aci_rx_q;

bool m_aci_isr_use = true;


static aci_pins_t *a_pins_local_ptr;

#ifdef HAL_ACI_TL_DEBUG
void m_aci_data_print(hal_aci_data_t *p_data) {
	const uint8_t length = p_data->buffer[0];
	uint8_t i;
	System::log.Write(length);
	System::log.Write(" :");
	for (i = 0; i <= length; i++) {
		System::log.Write(p_data->buffer[i], Print::Hex);
		System::log.Write(", ");
	}
	System::log.Line();
}
#endif

#ifdef HAL_ACI_TL_INTERRUPT
/*
  Interrupt service routine called when the RDYN line goes low. Runs the SPI transfer.
 */
static void m_aci_isr(void) {
	hal_aci_data_t data_to_send;
	hal_aci_data_t received_data;

	// Receive from queue
	if (!aci_queue_dequeue_from_isr(&aci_tx_q, &data_to_send)) {
		/* queue was empty, nothing to send */
		data_to_send.status_byte = 0;
		data_to_send.buffer[0] = 0;
	}

	// Receive and/or transmit data
	m_aci_spi_transfer(&data_to_send, &received_data);

	if (!aci_queue_is_full_from_isr(&aci_rx_q)
			&& !aci_queue_is_empty_from_isr(&aci_tx_q)) {
		m_aci_reqn_enable();
	}

	// Check if we received data
	if (received_data.buffer[0] > 0) {
		if (!aci_queue_enqueue_from_isr(&aci_rx_q, &received_data)) {
			/* Receive Buffer full.
			 Should never happen.
			 Spin in a while loop.
			 */
			while (1)
				;
		}

		// Disable ready line interrupt until we have room to store incoming messages
		if (aci_queue_is_full_from_isr(&aci_rx_q)) {
			m_aci_isr_use = false;
		}
	}

	return;
}
#endif

/*
 Checks the RDYN line and runs the SPI transfer if required.
 */
static void m_aci_event_check(void) {
	hal_aci_data_t data_to_send;
	hal_aci_data_t received_data;

	// No room to store incoming messages
	if (aci_queue_is_full(&aci_rx_q)) {
		return;
	}

	// If the ready line is disabled and we have pending messages outgoing we enable the request line

	if (a_pins_local_ptr->rdyn_pin->GetState()) {
		if (!aci_queue_is_empty(&aci_tx_q)) {
			m_aci_reqn_enable();
		}

		return;
	}

	// Receive from queue
	if (!aci_queue_dequeue(&aci_tx_q, &data_to_send)) {
		/* queue was empty, nothing to send */
		data_to_send.status_byte = 0;
		data_to_send.buffer[0] = 0;
	}

	// Receive and/or transmit data
	m_aci_spi_transfer(&data_to_send, &received_data);

	/* If there are messages to transmit, and we can store the reply, we request a new transfer */
	if (!aci_queue_is_full(&aci_rx_q) && !aci_queue_is_empty(&aci_tx_q)) {
		m_aci_reqn_enable();
	}

	// Check if we received data
	if (received_data.buffer[0] > 0) {
		if (!aci_queue_enqueue(&aci_rx_q, &received_data)) {
			/* Receive Buffer full.
			 Should never happen.
			 Spin in a while loop.
			 */
			while (1);
		}
	}

	return;
}

/** @brief Point the low level library at the ACI pins specified
 *  @details
 *  The ACI pins are specified in the application and a pointer is made available for
 *  the low level library to use
 */
static void m_aci_pins_set(aci_pins_t *a_pins_ptr) {
	a_pins_local_ptr = a_pins_ptr;
}

static inline void m_aci_reqn_disable(void) {
	a_pins_local_ptr->reqn_pin->Set();
}

static inline void m_aci_reqn_enable(void) {
	a_pins_local_ptr->reqn_pin->Reset();
}

static void m_aci_q_flush(void) {
	System::CriticalSection(true);
	/* re-initialize aci cmd queue and aci event queue to flush them*/
	aci_queue_init(&aci_tx_q);
	aci_queue_init(&aci_rx_q);
	System::CriticalSection(false);
}

static bool m_aci_spi_transfer(hal_aci_data_t *data_to_send,
		hal_aci_data_t *received_data) {
	uint8_t byte_cnt;
	uint8_t byte_sent_cnt;
	uint8_t max_bytes;

	m_aci_reqn_enable();

	// Send length, receive header
	byte_sent_cnt = 0;
	received_data->status_byte = spi_readwrite(
			data_to_send->buffer[byte_sent_cnt++]);
	// Send first byte, receive length from slave
	received_data->buffer[0] = spi_readwrite(
			data_to_send->buffer[byte_sent_cnt++]);
	if (0 == data_to_send->buffer[0]) {
		max_bytes = received_data->buffer[0];
	} else {
		// Set the maximum to the biggest size. One command byte is already sent
		max_bytes =
				(received_data->buffer[0] > (data_to_send->buffer[0] - 1)) ?
						received_data->buffer[0] :
						(data_to_send->buffer[0] - 1);
	}

	if (max_bytes > HAL_ACI_MAX_LENGTH) {
		max_bytes = HAL_ACI_MAX_LENGTH;
	}

	// Transmit/receive the rest of the packet
	for (byte_cnt = 0; byte_cnt < max_bytes; byte_cnt++) {
		received_data->buffer[byte_cnt + 1] = spi_readwrite(
				data_to_send->buffer[byte_sent_cnt++]);
	}

	// RDYN should follow the REQN line in approx 100ns
	m_aci_reqn_disable();

	return (max_bytes > 0);
}

#ifdef HAL_ACI_TL_DEBUG
void hal_aci_tl_debug_print(bool enable) {
	aci_debug_print = enable;
}
#else
void hal_aci_tl_debug_print(bool /*enable*/)
{
}
#endif

void hal_aci_tl_pin_reset(void) {
	if (a_pins_local_ptr->reset_pin != nullptr) {
		a_pins_local_ptr->reset_pin->Reset();
		System::DelayMs(100);
		a_pins_local_ptr->reset_pin->Set();
	}
}

bool hal_aci_tl_event_peek(hal_aci_data_t *p_aci_data) {
#ifdef HAL_ACI_TL_INTERRUPT
  if (!a_pins_local_ptr->interface_is_interrupt)
#endif
	{
		m_aci_event_check();
	}

	if (aci_queue_peek(&aci_rx_q, p_aci_data)) {
		return true;
	}

	return false;
}

bool hal_aci_tl_event_get(hal_aci_data_t *p_aci_data) {
#ifdef HAL_ACI_TL_INTERRUPT
  bool was_full;
#endif

#ifdef HAL_ACI_TL_INTERRUPT
  if (!a_pins_local_ptr->interface_is_interrupt && !aci_queue_is_full(&aci_rx_q))
#else
	if (!aci_queue_is_full(&aci_rx_q))
#endif
			{
		m_aci_event_check();
	}

#ifdef HAL_ACI_TL_INTERRUPT
	was_full = aci_queue_is_full(&aci_rx_q);
#else
  aci_queue_is_full(&aci_rx_q);
#endif

	if (aci_queue_dequeue(&aci_rx_q, p_aci_data)) {
#ifdef HAL_ACI_TL_DEBUG
		if (aci_debug_print) {
			System::log.Write(" E");
			m_aci_data_print(p_aci_data);
		}
#endif

#ifdef HAL_ACI_TL_INTERRUPT
    if (was_full && a_pins_local_ptr->interface_is_interrupt)
	  {
      /* Enable RDY line interrupt again */
    	m_aci_isr_use = true;
    }
#endif

		/* Attempt to pull REQN LOW since we've made room for new messages */
		if (!aci_queue_is_full(&aci_rx_q) && !aci_queue_is_empty(&aci_tx_q)) {
			m_aci_reqn_enable();
		}

		return true;
	}

	return false;
}

#ifdef HAL_ACI_TL_DEBUG
void hal_aci_tl_init(aci_pins_t *a_pins, bool debug)
#else
void hal_aci_tl_init(aci_pins_t *a_pins, bool /*debug*/)
#endif
		{
#ifdef HAL_ACI_TL_DEBUG
	aci_debug_print = debug;
#endif

	/* Needs to be called as the first thing for proper intialization*/
	m_aci_pins_set(a_pins);


	a_pins->spi->SetParameters({
		.mode = ASPI::Mode::Master,
		.direction = ASPI::Direction::TxRx,
		.clockPolarity = ASPI::ClockPolarity::Low,
		.clockPhase = ASPI::ClockPhase::Edge1,
		.firstBit = ASPI::FirstBit::LSB,
		.maxSpeedHz = 2000
	});

	/* Initialize the ACI Command queue. This must be called after the delay above. */
	aci_queue_init(&aci_tx_q);
	aci_queue_init(&aci_rx_q);


	//Configure the IO lines
#ifdef HAL_ACI_TL_INTERRUPT
	if (a_pins->interface_is_interrupt) {
		a_pins->rdyn_pin->SetParameters({ AGPIO::Mode::InterruptFalling, AGPIO::Pull::Up });
	} else {
		a_pins->rdyn_pin->SetParameters({ AGPIO::Mode::Input, AGPIO::Pull::Up });
	}
#else
	a_pins->rdyn_pin->SetParameters({ AGPIO::Mode::Input, AGPIO::Pull::Up });
#endif

	a_pins->reqn_pin->Set().SetParameters({ AGPIO::Mode::Output });

	if (a_pins->reset_pin != nullptr) {
		a_pins->reset_pin->Reset().SetParameters({ AGPIO::Mode::Output });
	}


	/* Pin reset the nRF8001, required when the nRF8001 setup is being changed */
	hal_aci_tl_pin_reset();


	// TODO: Set the nRF8001 to a known state as required by the datasheet
	a_pins->reqn_pin->Set();


	System::DelayMs(30); //Wait for the nRF8001 to get hold of its lines - the lines float for a few ms after the reset

#ifdef HAL_ACI_TL_INTERRUPT
  /* Attach the interrupt to the RDYN line as requested by the caller */
  if (a_pins->interface_is_interrupt) {
	a_pins->rdyn_pin->onInterrupt = [](bool state) {
		if(m_aci_isr_use) {
			m_aci_isr();
		}
	};
  }
#endif
}

bool hal_aci_tl_send(hal_aci_data_t *p_aci_cmd) {
	const uint8_t length = p_aci_cmd->buffer[0];
	bool ret_val = false;

	if (length > HAL_ACI_MAX_LENGTH) {
		return false;
	}

	ret_val = aci_queue_enqueue(&aci_tx_q, p_aci_cmd);
	if (ret_val) {
		if (!aci_queue_is_full(&aci_rx_q)) {
			// Lower the REQN only when successfully enqueued
			m_aci_reqn_enable();
		}

#ifdef HAL_ACI_TL_DEBUG
		if (aci_debug_print) {
			System::log.Write("C"); //ACI Command
			m_aci_data_print(p_aci_cmd);
		}
#endif
	}

	return ret_val;
}

static uint8_t spi_readwrite(const uint8_t aci_byte) {

	System::CriticalSection(true);
	auto read = a_pins_local_ptr->spi->WriteRead<uint8>(aci_byte).data;
	System::CriticalSection(false);

	return read;
}

bool hal_aci_tl_rx_q_empty(void) {
	return aci_queue_is_empty(&aci_rx_q);
}

bool hal_aci_tl_rx_q_full(void) {
	return aci_queue_is_full(&aci_rx_q);
}

bool hal_aci_tl_tx_q_empty(void) {
	return aci_queue_is_empty(&aci_tx_q);
}

bool hal_aci_tl_tx_q_full(void) {
	return aci_queue_is_full(&aci_tx_q);
}

void hal_aci_tl_q_flush(void) {
	m_aci_q_flush();
}

#endif

