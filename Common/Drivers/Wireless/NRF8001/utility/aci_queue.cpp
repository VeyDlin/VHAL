#include <PortAdapters.h>
#ifdef AUSED_SPI_ADAPTER

#include "hal_aci_tl.h"
#include "aci_queue.h"
#include "ble_assert.h"

void aci_queue_init(aci_queue_t *aci_q)
{
  uint8_t loop;

  ble_assert(NULL != aci_q);

  aci_q->head = 0;
  aci_q->tail = 0;
  for(loop=0; loop<ACI_QUEUE_SIZE; loop++)
  {
    aci_q->aci_data[loop].buffer[0] = 0x00;
    aci_q->aci_data[loop].buffer[1] = 0x00;
  }
}

bool aci_queue_dequeue(aci_queue_t *aci_q, hal_aci_data_t *p_data)
{
  ble_assert(NULL != aci_q);
  ble_assert(NULL != p_data);

  if (aci_queue_is_empty(aci_q))
  {
    return false;
  }

  memcpy((uint8_t *)p_data, (uint8_t *)&(aci_q->aci_data[aci_q->head % ACI_QUEUE_SIZE]), sizeof(hal_aci_data_t));
  ++aci_q->head;

  return true;
}

#ifdef HAL_ACI_TL_INTERRUPT
bool aci_queue_dequeue_from_isr(aci_queue_t *aci_q, hal_aci_data_t *p_data)
{
  ble_assert(NULL != aci_q);
  ble_assert(NULL != p_data);

  if (aci_queue_is_empty_from_isr(aci_q))
  {
    return false;
  }

  memcpy((uint8_t *)p_data, (uint8_t *)&(aci_q->aci_data[aci_q->head % ACI_QUEUE_SIZE]), sizeof(hal_aci_data_t));
  ++aci_q->head;

  return true;
}
#endif

bool aci_queue_enqueue(aci_queue_t *aci_q, hal_aci_data_t *p_data)
{
  const uint8_t length = p_data->buffer[0];

  ble_assert(NULL != aci_q);
  ble_assert(NULL != p_data);

  if (aci_queue_is_full(aci_q))
  {
    return false;
  }

  aci_q->aci_data[aci_q->tail % ACI_QUEUE_SIZE].status_byte = 0;
  memcpy((uint8_t *)&(aci_q->aci_data[aci_q->tail % ACI_QUEUE_SIZE].buffer[0]), (uint8_t *)&p_data->buffer[0], length + 1);
  ++aci_q->tail;

  return true;
}

#ifdef HAL_ACI_TL_INTERRUPT
bool aci_queue_enqueue_from_isr(aci_queue_t *aci_q, hal_aci_data_t *p_data)
{
  const uint8_t length = p_data->buffer[0];

  ble_assert(NULL != aci_q);
  ble_assert(NULL != p_data);

  if (aci_queue_is_full_from_isr(aci_q))
  {
    return false;
  }

  aci_q->aci_data[aci_q->tail % ACI_QUEUE_SIZE].status_byte = 0;
  memcpy((uint8_t *)&(aci_q->aci_data[aci_q->tail % ACI_QUEUE_SIZE].buffer[0]), (uint8_t *)&p_data->buffer[0], length + 1);
  ++aci_q->tail;

  return true;
}
#endif

bool aci_queue_is_empty(aci_queue_t *aci_q)
{
  bool state = false;

  ble_assert(NULL != aci_q);

#ifdef HAL_ACI_TL_INTERRUPT
  System::CriticalSection(true);
#endif
  if (aci_q->head == aci_q->tail)
  {
    state = true;
  }
#ifdef HAL_ACI_TL_INTERRUPT
  System::CriticalSection(false);
#endif

  return state;
}

bool aci_queue_is_empty_from_isr(aci_queue_t *aci_q)
{
  ble_assert(NULL != aci_q);

  return aci_q->head == aci_q->tail;
}

bool aci_queue_is_full(aci_queue_t *aci_q)
{
  bool state;

  ble_assert(NULL != aci_q);

#ifdef HAL_ACI_TL_INTERRUPT
  //This should be done in a critical section
  System::CriticalSection(true);
#endif

  state = (aci_q->tail == aci_q->head + ACI_QUEUE_SIZE);

#ifdef HAL_ACI_TL_INTERRUPT
  System::CriticalSection(false);
  //end
#endif

  return state;
}

bool aci_queue_is_full_from_isr(aci_queue_t *aci_q)
{
  ble_assert(NULL != aci_q);

  return (aci_q->tail == aci_q->head + ACI_QUEUE_SIZE);
}

bool aci_queue_peek(aci_queue_t *aci_q, hal_aci_data_t *p_data)
{
  ble_assert(NULL != aci_q);
  ble_assert(NULL != p_data);

  if (aci_queue_is_empty(aci_q))
  {
    return false;
  }

  memcpy((uint8_t *)p_data, (uint8_t *)&(aci_q->aci_data[aci_q->head % ACI_QUEUE_SIZE]), sizeof(hal_aci_data_t));

  return true;
}

bool aci_queue_peek_from_isr(aci_queue_t *aci_q, hal_aci_data_t *p_data)
{
  ble_assert(NULL != aci_q);
  ble_assert(NULL != p_data);

  if (aci_queue_is_empty_from_isr(aci_q))
  {
    return false;
  }

  memcpy((uint8_t *)p_data, (uint8_t *)&(aci_q->aci_data[aci_q->head % ACI_QUEUE_SIZE]), sizeof(hal_aci_data_t));

  return true;
}

#endif
