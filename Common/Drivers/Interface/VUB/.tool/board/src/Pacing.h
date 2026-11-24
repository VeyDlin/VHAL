#pragma once
#include <cstddef>


namespace VubBoard {
	// Whether one more payload may be sent. A payload counts as in flight from its send until its echo is checked, so the payloads in
	// flight are the sent ones less the echoed ones. A payload is sent only while fewer than limit of them are in flight. The board keeps
	// each received payload in its echo queue until it hands the echo to the delivery layer, and the queue holds four payloads. A limit of
	// four therefore means the queue never overflows, whatever the link drops. An echo ahead of the sends (a bogus one) counts as nothing
	// in flight.
	inline bool MaySend(std::size_t sent, std::size_t echoed, std::size_t limit) {
		if (echoed >= sent) {
			return limit > 0;
		}

		return sent - echoed < limit;
	}
}
