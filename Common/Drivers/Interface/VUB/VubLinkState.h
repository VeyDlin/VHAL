#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>


// What a stack does with its packets while the connection is down. Wait (the default): the queued packets stay queued, Send queues until
// the queue is full and then returns busy, and the queued packets go after the connection is up again. Fail: the queued packets are
// dropped and counted as linkDown, and Send returns noConnection while the connection is down.
enum class VubDown : uint8 {
	Wait,
	Fail
};


// The object a link reports its state into. The stack gives it to the link (SetStateSink); the link calls Report(true) when it is up and
// Report(false) when it is down. Report only records the state, so the link may call it from an interrupt. A sink with no function, as
// the stack's destructor gives back, reports into nothing.
class VubStateSink {
public:
	VubStateSink() = default;


	VubStateSink(void* ownerObject, void (*reportFunction)(void*, bool)) : owner(ownerObject), reportFn(reportFunction) {
	}


	void Report(bool up) const {
		if (reportFn != nullptr) {
			reportFn(owner, up);
		}
	}


private:
	void* owner = nullptr;
	void (*reportFn)(void*, bool) = nullptr;
};


// The connection state a stack keeps when its link reports it, or a layer reports its peer. The fields that the link's report writes are
// volatile: the interrupt writes them and Process reads them. The rest belong to the main context.
struct VubLinkRecord {
	volatile bool reportedUp = false;
	volatile uint16 reportedFalls = 0;
	uint16 seenFalls = 0;
	bool linkUp = false;
	bool peerUp = true;
	bool connected = false;
	uint16 downs = 0;
	uint16 dropped = 0;


	// Records what the link reports. Called from the link's interrupt: the stack acts on it in Process. The state is written before a
	// fall is counted, so a Process that reads the count first reads a state at least as new as the fall.
	void Report(bool up) {
		reportedUp = up;
		if (!up) {
			reportedFalls = static_cast<uint16>(reportedFalls + 1);
		}
	}
};


// Gives the state sink back to the link when the stack is destroyed, so the link does not report into a stack that is gone. A link that
// does not report has no sink, and this holds nothing.
template<bool Enabled, typename Link>
class VubDetach {
public:
	explicit VubDetach(Link* linkObject) : link(linkObject) {
	}


	VubDetach(const VubDetach&) = delete;
	VubDetach& operator=(const VubDetach&) = delete;


	~VubDetach() {
		link->SetStateSink(VubStateSink());
	}


private:
	Link* link;
};


template<typename Link>
class VubDetach<false, Link> {
public:
	explicit VubDetach(Link*) {
	}
};
