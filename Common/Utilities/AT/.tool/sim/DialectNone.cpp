// The server of a set with no dialect item: no echo, verbose, quiet, register or CME code. It is compiled for the target with -Os and
// compared with DialectAll.cpp, the same commands with every dialect item, by run.sh. The difference is the dialect's code alone.
#include <Utilities/AT/AtServer.h>

struct SizeApp {
	int value = 0;
};


struct SizePrint : public Print {
	void WriteRaw(const char* data, size_t size) override {
		static_cast<void>(data);
		static_cast<void>(size);
	}
};


Result<int16, AtStatus> SizeTemp(AtCall<SizeApp> call) {
	return static_cast<int16>(call.app.value);
}


AtStatus SizeLed(AtCall<SizeApp> call, bool on) {
	call.app.value = on ? 1 : 0;
	return AtStatus{};
}


AtStatus SizeRun(AtCall<SizeApp> call) {
	call.app.value++;
	return AtStatus{};
}


using SizeSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+RUN">().Run();
}>;


using SizeBinding = AtServerBinding<
	AtOn<"+TEMP", &SizeTemp>,
	AtOn<"+LED", &SizeLed>,
	AtOn<"+RUN", &SizeRun>
>;


void SizeServe(SizeApp& app, Print& out) {
	AtServer<SizeSet, SizeApp, SizeBinding> server(app, out);

	server.Feed(static_cast<uint8>('A'));
	server.Feed(static_cast<uint8>('T'));
	server.Feed(static_cast<uint8>('+'));
	server.Feed(static_cast<uint8>('L'));
	server.Feed(static_cast<uint8>('E'));
	server.Feed(static_cast<uint8>('D'));
	server.Feed(static_cast<uint8>('='));
	server.Feed(static_cast<uint8>('1'));
	server.Feed(static_cast<uint8>('\r'));
	server.Process();
	server.DroppedBytes();
}
