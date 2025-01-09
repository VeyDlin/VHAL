#pragma once
#include "Port/Port.h"


namespace OSAdapter {
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