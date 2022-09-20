#pragma once
#include "../EventMode.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"
#include "event_groups.h"
#include "portmacro.h"
#include "FreeRTOSConfig.h"

#include <chrono>
#include <limits>

using namespace std::chrono_literals;


namespace OSAdapter {
	extern "C" void vPortSVCHandler(void);
	extern "C" void xPortPendSVHandler(void);
	extern "C" void xPortSysTickHandler(void);

	enum class ThreadPriority : std::uint8_t {
		clear = 0,
		idle = 1,
		low = 10,
		belowNormal = 20,
		normal = 30,
		aboveNormal = 80,
		high = 90,
		realtime = 255
	};

	using tTaskContext = StaticTask_t;
	using tTaskHandle = TaskHandle_t;
	using tStack = StackType_t;

	using tTimerContext = StaticTimer_t;
	using tTimerHandle = TimerHandle_t;

	using tTaskEventMask = std::uint32_t;
	using tTime = TickType_t;

	using tEventHandle = EventGroupHandle_t;
	using tEvent = StaticEventGroup_t;
	using tEventBits = EventBits_t;

	using tMailBoxContext = StaticQueue_t;
	using tMailBoxHandle = QueueHandle_t;

	using tMutex = StaticSemaphore_t;
	using tMutexHandle = SemaphoreHandle_t;

	using TicksPerSecond = std::chrono::duration<tTime, std::ratio<portTICK_PERIOD_MS, 1000>>;


	struct RtosWrapper {
		static constexpr TicksPerSecond waitForEver = static_cast<TicksPerSecond>(portMAX_DELAY);
		static constexpr TicksPerSecond notWait = static_cast<TicksPerSecond>(0);

		static inline bool wInHandlerMode() {
			return __get_IPSR() != 0;
		}


		template<typename Rtos, typename T>
		static inline void wCreateThreadStatic(T &thread, const char *pName, ThreadPriority prior, const std::uint16_t stackDepth, tStack *pStack) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
			if(pStack != nullptr) {
				thread.handle = xTaskCreateStatic(
					static_cast<TaskFunction_t>(Rtos::Run),
					pName,
					stackDepth,
					&thread,
					static_cast<uint32_t>(prior),
					pStack,
					&thread.context
				);
			}
#else
			static_assert("configSUPPORT_STATIC_ALLOCATION == 0");
#endif
		}


		template<typename Rtos, typename T>
		static inline void wCreateThread(T &thread, const char *pName, ThreadPriority prior, const std::uint16_t stackDepth) {
			thread.handle = (xTaskCreate(
				Rtos::Run,
				pName,
				stackDepth,
				&thread,
				static_cast<uint32_t>(prior),
				&thread.handle
			) == pdTRUE) ? thread.handle : nullptr;
		}


		inline static void wStart() {
			vTaskStartScheduler();
		}


		inline static bool wIsSchedulerRun() {
#if (INCLUDE_xTaskGetSchedulerState == 1 )
			return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
#else
			SystemAbort();
#endif
		}


		inline static void wHandleSvcInterrupt() {
			vPortSVCHandler();
		}


		inline static void wHandleSysTickInterrupt() {
#if (INCLUDE_xTaskGetSchedulerState == 1 )
			if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
#endif
				xPortSysTickHandler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
			}
#endif
		}


		inline static void wSleep(const tTime timeOut) {
			vTaskDelay(timeOut);
		}


		inline static void wEnterCriticalSection() {
			taskENTER_CRITICAL();
		}


		inline static void wLeaveCriticalSection() {
			taskEXIT_CRITICAL();
		}


		inline static void wSignal(tTaskHandle const &taskHandle, const tTaskEventMask mask) {
			if (wInHandlerMode()) {
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				if(xTaskNotifyFromISR(taskHandle, mask, eSetBits, &xHigherPriorityTaskWoken) != pdFAIL) {
					portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
				}
			} else {
				xTaskNotify(taskHandle, mask, eSetBits);
			}
		}


		inline static tTaskEventMask wWaitForSignal(const tTaskEventMask mask, tTime timeOut) {
			uint32_t ulNotifiedValue = 0U;
			xTaskNotifyWait(0U, std::numeric_limits<uint32>::max(), &ulNotifiedValue, timeOut);
			return (ulNotifiedValue & mask);
		}


		inline static bool wTaskSuspend(tTaskHandle const &taskHandle) {
#if (INCLUDE_vTaskSuspend == 1)
			vTaskSuspend(taskHandle);
			return true;
#else
			return false;
#endif
		}


		inline static void wTaskResume(tTaskHandle const &taskHandle) {
			if (wInHandlerMode()) {
				xTaskResumeFromISR(taskHandle);
			} else {
				vTaskResume(taskHandle);
			}
		}


		inline static tEventHandle wCreateEvent(tEvent &event) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
			return xEventGroupCreateStatic(&event);
#else
			return xEventGroupCreate();
#endif
		}


		inline static void wDeleteEvent(tEventHandle &eventHandle) {
			vEventGroupDelete(eventHandle);
		}


		inline static void wSignalEvent(tEventHandle const &eventHandle, const tEventBits mask) {
			if (wInHandlerMode()) {
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				if (xEventGroupSetBitsFromISR(eventHandle, mask, &xHigherPriorityTaskWoken) != pdFAIL) {
					portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
				}
			} else {
				xEventGroupSetBits(eventHandle, mask);
			}
		}


		inline static tEventBits wWaitEvent(tEventHandle const &eventHandle, const tEventBits mask, tTime timeOut, OSAdapter::EventMode mode) {
			BaseType_t xWaitForAllBits = pdFALSE;
			if (mode == OSAdapter::EventMode::waitAnyBits) {
				xWaitForAllBits = pdFALSE;
			}
			return xEventGroupWaitBits(eventHandle, mask, pdTRUE, xWaitForAllBits, timeOut);
		}


		inline static tMutexHandle wCreateMutex(tMutex &mutex) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
			return xSemaphoreCreateMutexStatic(&mutex);
#else
			return xSemaphoreCreateMutex();
#endif
		}


		inline static void wDeleteMutex(tMutexHandle &handle) {
			vSemaphoreDelete(handle);
		}


		inline static bool wLockMutex(tMutexHandle const &handle, tTime timeOut) {
			if (wInHandlerMode()) {
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				auto status = xSemaphoreTakeFromISR(handle,  &xHigherPriorityTaskWoken);
			    if (status != pdFAIL) {
			    	portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
			    }
			    return static_cast<bool>(status);
			} else {
				return static_cast<bool>(xSemaphoreTake(handle, timeOut));
			}
		}


		inline static void wUnLockMutex(tMutexHandle const &handle) {
			if (wInHandlerMode()) {
				xSemaphoreGiveFromISR(handle, nullptr);
			} else {
				xSemaphoreGive(handle);
			}
		}


		inline static void wSleepUntil(tTime &last, const tTime timeOut) {
			#if INCLUDE_vTaskDelayUntil
				vTaskDelayUntil(&last, timeOut);
			#else
				SystemAbort();
			#endif
		}


		inline static tTime wGetTicks() {
			if (wInHandlerMode()) {
				return xTaskGetTickCountFromISR();
			} else {
				return xTaskGetTickCount();
			}
		}


		inline static bool wMailBoxPut(tMailBoxHandle &handle, const void *pItem, tTime timeOut) {
			if (wInHandlerMode()) {
				return (xQueueSendFromISR(handle, pItem, nullptr) == pdTRUE);
			} else {
				return (xQueueSend(handle, pItem, timeOut) == pdTRUE);
			}
		}


		inline static tMailBoxHandle wMailBoxCreate(std::uint16_t length, std::uint16_t itemSize, std::uint8_t *pBuffer, tMailBoxContext &context) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
			return xQueueCreateStatic(length, itemSize, pBuffer, &context);
#else
			return xQueueCreate(length, itemSize);
#endif
		}


		inline static bool wMailBoxGet(tMailBoxHandle &handle, void *pItem, tTime timeOut) {
			if (wInHandlerMode()) {
				return (xQueueReceiveFromISR(handle, pItem, nullptr) == pdTRUE);
			} else {
				return (xQueueReceive(handle, pItem, timeOut) == pdTRUE);
			}
		}


		inline static void wMailBoxDelete(tMailBoxHandle &queue) {
			vQueueDelete(queue);
		}
	};
}

