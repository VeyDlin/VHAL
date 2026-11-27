// The server of a set with every dialect item: ATE, ATV, ATQ, ATS3 and ATS4, AT+CMEE, and CME text. The commands and the serving code
// are those of DialectNone.cpp, so the size/Dialect.cmake measurement reads the flash cost of the dialect from the difference of the two objects.
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
		.Dialect(AtDialect().Echo().Verbose().Quiet().Registers().CmeCommand().CmeText<3, "operation not allowed">())
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
