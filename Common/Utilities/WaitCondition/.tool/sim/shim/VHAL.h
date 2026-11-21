#pragma once
// Desktop stand-in for the VHAL umbrella header, for the WaitCondition host tests. The real System declaration is kept, so the
// shared header compiles unchanged. The tick is the real System::tickLow, which GetTick32 reads inline, so a test sets it and
// the wait sees the same load a port does. Nothing advances it but the test. The RTOS delay handle is defined here because
// System.cpp is not linked.
#include <System/System.h>
#include <functional>


inline volatile uint32 System::tickLow = 0;

inline std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;
