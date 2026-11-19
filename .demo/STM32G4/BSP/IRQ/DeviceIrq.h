#pragma once

#ifdef __cplusplus
	extern "C" {
#endif

	void USB_LP_IRQHandler(void);
	void USB_HP_IRQHandler(void);
	void USART1_IRQHandler(void);

#ifdef __cplusplus
	}
#endif
