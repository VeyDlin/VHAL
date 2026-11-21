#pragma once

inline constexpr int ExitOk = 0;
// A check could not run: no board, no COM port, a device that did not answer, a tool that is missing.
inline constexpr int ExitDevice = 1;
inline constexpr int ExitUsage = 2;
// Every check ran and at least one of them found a wrong result.
inline constexpr int ExitFailed = 3;
