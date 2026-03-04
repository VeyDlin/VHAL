#pragma once

#include "RTOS.h"
#include "Thread.h"

#if defined(VHAL_RTOS_TIMER)
	#include "Timer.h"
#endif

#if defined(VHAL_RTOS_CRITICAL_SECTION)
    #include "CriticalSection.h"
#endif

#if defined(VHAL_RTOS_EVENT)
    #include "Event.h"
#endif

#if defined(VHAL_RTOS_MAILBOX)
    #include "Mailbox.h"
#endif

#if defined(VHAL_RTOS_MUTEX)
	#include "Mutex.h"
#endif