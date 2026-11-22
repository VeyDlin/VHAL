#pragma once
#include "AtArgument.h"
#include "AtError.h"
#include "AtForms.h"
#include "AtRow.h"
#include "Modules/AtDataMode.h"
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>


// The handler binding of a server. A handler is a static function or a captureless lambda (+[]), bound to a command name:
//   AtServerBinding<AtOn<"+TEMP", &Temp>, AtOn<"+LED", &Led>>
// A name can be bound more than once, one handler for each form of the command. The signature of a handler says which form it serves:
//   Read<T>, Run<T>:  Result<T, AtStatus> Name(AtCall<App>)
//   Read(row), Run(row): Result<Row, AtStatus> Name(AtCall<App>), which writes one info line
//   ReadRows, Rows:   AtStatus Name(AtCall<App>, AtRowWriter<Row>&), which adds rows with out.Add(row)
//   Run:              AtStatus Name(AtCall<App>)
//   Write, Value:     AtStatus Name(AtCall<App>, args...), where each argument takes the handler type of its declared type
// A string argument arrives as std::string_view, and an optional one as std::optional. A Test form has no handler: its text comes from
// the declared ranges.


// The first parameter of every server handler: the application, which the handler reads and changes.
template<typename App>
struct AtCall {
	App& app;
};


// One handler for one command name. The name is checked against the description by the server, and the handler's signature is checked
// against the forms of that command.
template<AtText Name, auto Handler>
struct AtOn {
	static constexpr decltype(Name) nameText = Name;
	static constexpr std::string_view name = Name.View();
	static constexpr decltype(Handler) handler = Handler;
	using HandlerType = decltype(Handler);
};


// The handlers of a server, in any order.
template<typename... Bindings>
struct AtServerBinding {
	using Items = std::tuple<Bindings...>;
};


// One handler for a URC of the client, by its prefix. The handler's parameters are the URC's arguments, each with its handler type:
//   void OnIpd(uint16 length)          for Urc<"+IPD">(Argument<uint16>("length"))
// A URC with no handler is consumed and ignored. The client checks the signature against the description.
template<AtText Name, auto Handler>
struct AtUrcOn : AtOn<Name, Handler> {
};


// The URC handlers of a client, in any order.
template<typename... Bindings>
struct AtClientBinding {
	using Items = std::tuple<Bindings...>;
};


// The type of a URC handler for a list of arguments: void(HandlerType...). A URC with Data() takes the arguments before it, then the offset of
// the chunk (the length's type), the chunk as a span of bytes, and whether the chunk is the last one.
template<typename Arguments, typename Data = AtNoData>
struct AtUrcHandler;


template<typename... Arguments>
struct AtUrcHandler<std::tuple<Arguments...>, AtNoData> {
	using Type = void (*)(typename Arguments::HandlerType...);
};


template<typename Length, typename... Arguments>
struct AtUrcHandler<std::tuple<Arguments...>, AtDataLength<Length>> {
	using Type = void (*)(typename Arguments::HandlerType..., Length, std::span<const uint8>, bool);
};


// The signature a handler has for a form. The test form has none, so it is void.
template<typename App, typename Form>
struct AtFormHandler {
	using Type = void;
};


// The signature of a handler with the arguments of a write or value form: the application, then each argument as the handler gets it.
template<typename App, typename Tuple>
struct AtArgumentsHandler;


template<typename App, typename... Arguments>
struct AtArgumentsHandler<App, std::tuple<Arguments...>> {
	using Type = AtStatus (*)(AtCall<App>, typename Arguments::HandlerType...);
};


template<typename App, typename T>
struct AtFormHandler<App, AtReadForm<T>> {
	using Type = Result<T, AtStatus> (*)(AtCall<App>);
};


template<typename App, typename T>
struct AtFormHandler<App, AtRunValueForm<T>> {
	using Type = Result<T, AtStatus> (*)(AtCall<App>);
};


template<typename App, typename RowDecl>
struct AtFormHandler<App, AtReadRecordForm<RowDecl>> {
	using Type = Result<typename RowDecl::RowType, AtStatus> (*)(AtCall<App>);
};


template<typename App, typename RowDecl>
struct AtFormHandler<App, AtRunRecordForm<RowDecl>> {
	using Type = Result<typename RowDecl::RowType, AtStatus> (*)(AtCall<App>);
};


template<typename App, typename RowDecl>
struct AtFormHandler<App, AtReadRowsForm<RowDecl>> {
	using Type = AtStatus (*)(AtCall<App>, AtRowWriter<typename RowDecl::RowType>&);
};


template<typename App, typename RowDecl>
struct AtFormHandler<App, AtRunRowsForm<RowDecl>> {
	using Type = AtStatus (*)(AtCall<App>, AtRowWriter<typename RowDecl::RowType>&);
};


template<typename App>
struct AtFormHandler<App, AtRunForm> {
	using Type = AtStatus (*)(AtCall<App>);
};


template<typename App, typename... Arguments>
struct AtFormHandler<App, AtWriteForm<Arguments...>> {
	using Type = typename AtArgumentsHandler<App, std::tuple<Arguments...>>::Type;
};


template<typename App, typename... Arguments>
struct AtFormHandler<App, AtValueForm<Arguments...>> {
	using Type = typename AtArgumentsHandler<App, std::tuple<Arguments...>>::Type;
};


// True when the handler type is the signature of one of the forms in the tuple.
template<typename App, typename Handler, typename Tuple>
struct AtFitsForms;


template<typename App, typename Handler, typename... Forms>
struct AtFitsForms<App, Handler, std::tuple<Forms...>> {
	static constexpr bool value = (std::is_same_v<Handler, typename AtFormHandler<App, Forms>::Type> || ...);
};


// The number of bindings with the name and the signature Expected.
template<typename Expected, typename... Bindings>
constexpr std::size_t AtCountHandlers(std::string_view name) {
	static_cast<void>(name);
	return ((std::is_same_v<typename Bindings::HandlerType, Expected> && (Bindings::name == name) ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
}


// The handler of a form: the one binding with the name and the signature Expected. The result is null when there is none, and the
// server checks at compile time that there is one for each form.
template<typename Expected, typename Tuple>
struct AtPickHandler;


template<typename Expected, typename... Bindings>
struct AtPickHandler<Expected, std::tuple<Bindings...>> {
	static Expected Find(std::string_view name) {
		Expected found = nullptr;
		(Take<Bindings>(name, found), ...);
		static_cast<void>(name);
		return found;
	}


private:
	template<typename Binding>
	static void Take(std::string_view name, Expected& found) {
		if constexpr (std::is_same_v<typename Binding::HandlerType, Expected>) {
			if (Binding::name == name) {
				found = Binding::handler;
			}
		}
	}
};
