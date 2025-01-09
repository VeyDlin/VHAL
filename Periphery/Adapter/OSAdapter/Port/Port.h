#pragma once

// TODO: use adapter_config.h
#define ADAPTERCONFIG_USEFREERTOS 1


#if (ADAPTERCONFIG_USEFREERTOS == 1)
    #include "FreeRTOSAdapter.h"
#endif