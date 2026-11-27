#pragma once
// Stand-in for the VHAL umbrella header, for the size programs only. The CompactStreamingProtocol header includes it for System::GetMs,
// and the real one selects a port, which a host or a probe compile does not have.
#include <System/System.h>
