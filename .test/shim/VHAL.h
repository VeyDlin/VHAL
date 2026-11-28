#pragma once
// Host stand-in for the VHAL umbrella header, shared by the host tests of the modules. The real System declaration is kept, so the
// module headers compile unchanged, and the System functions that a portable module reaches come from VhalSystem.h. A module that
// has its own shim keeps it; a module without one gets this file.
#include <VhalSystem.h>
