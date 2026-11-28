#include <VhalCheck.h>
#include <Utilities/Serialization/JSON/JSON.h>
#include <Utilities/Encoding/Base/Base64.h>


// Compile check: the public headers of JSON and Base64 build. The program builds and runs, so a header that does not compile fails
// the test. The frame's shared shim is on the include path of every host test; the header-alone check of the frame covers the
// headers without it.


int main() {
	return VhalCheck::Finish("TestPublicHeaders");
}
