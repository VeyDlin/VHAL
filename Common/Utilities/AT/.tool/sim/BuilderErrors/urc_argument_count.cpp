// expect: a URC is given a different number of arguments than it declares
#include <Utilities/AT/AtServer.h>

struct App {
};

struct NullPrint : public Print {
	void WriteRaw(const char* data, size_t size) override {
		static_cast<void>(data);
		static_cast<void>(size);
	}
};

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Run()
		.Urc<"+IPD">(Argument<uint16>("length"));
}>;

using Binding = AtServerBinding<AtOn<"+TEMP", +[](AtCall<App> call) -> AtStatus {
	static_cast<void>(call);
	return AtStatus{};
}>>;

void Queue(App& app, Print& out) {
	AtServer<Set, App, Binding> server(app, out);
	server.Urc<"+IPD">(static_cast<uint16>(1), static_cast<uint16>(2));
}
