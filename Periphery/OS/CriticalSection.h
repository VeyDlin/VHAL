#pragma once
#include "Port/Wrapper.h"


namespace OS {
	class CriticalSection {
	public:
		inline CriticalSection() {
			RtosWrapper::wEnterCriticalSection();
		}

		inline ~CriticalSection() {
			RtosWrapper::wLeaveCriticalSection();
		}
	};
}
