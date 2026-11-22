#pragma once
#include <Utilities/Result/ResultStatus.h>
#include <VHAL.h>
#include <Utilities/WaitCondition/WaitCondition.h>


// The await module of the client. A client that lists AtAwait has the Await calls: AwaitRead, AwaitRun, AwaitWrite, AwaitValue, AwaitRows,
// AwaitReadRows and AwaitSendData. Each is the plain call, and it returns the reply instead of calling a handler. It waits in a task with
// WaitCondition::WaitFor<RtosYield>, while another task runs Process(). It must never run in the Process() context, which it would wait for.
// The module is a type, listed among the client's modules: AtClient<Set, Binding, AtQueue<2>, AtAwait>. A client without it has no Await
// code, and no reference to WaitCondition or to the RTOS handle.


struct AtAwait {
};
