#pragma once


// The echo module of the client. A module that lists AtEcho drops a line that is the text of the request it sent, when the module echoes
// the request back as its first line (ATE1). Without it the echoed line is an unknown line and is ignored. The module is a type, listed
// among the client's modules: AtClient<Set, Binding, AtQueue<2>, AtEcho>.


struct AtEcho {
};
