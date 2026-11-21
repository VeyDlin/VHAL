#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <windows.h>

#include "Text.h"

// Device paths and IDs are ASCII, and they are compared as ASCII. A wide character above 0x7F is shown as '?', the way usbbench shows it.
inline std::wstring WidenAscii(const std::string& text) {
	std::wstring wide;
	for (char character : text) {
		wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
	}

	return wide;
}


inline std::string NarrowAscii(const std::wstring& text) {
	std::string narrow;
	for (wchar_t character : text) {
		narrow.push_back(character < 0x80 ? static_cast<char>(character) : '?');
	}

	return narrow;
}


// File paths and the text shown to the user are UTF-8: the console runs in UTF-8 (main sets its output code page), and a name such as a
// user's folder or an endpoint's name keeps its characters. An empty result means the text could not be converted.
inline std::wstring WidenUtf8(const std::string& text) {
	if (text.empty()) {
		return L"";
	}

	int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (length <= 0) {
		return L"";
	}

	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
	return wide;
}


inline std::string NarrowUtf8(const std::wstring& text) {
	if (text.empty()) {
		return "";
	}

	int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0) {
		return "";
	}

	std::string narrow(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
	return narrow;
}


// A GUID as the container IDs are compared: eight, four, four, four and twelve hex digits, lower case, without braces. The pure code
// accepts braces too, so the form only has to match on both sides.
inline std::string GuidText(const GUID& guid) {
	std::string text;
	AppendFormat(
		text,
		"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		static_cast<unsigned long>(guid.Data1),
		static_cast<unsigned int>(guid.Data2),
		static_cast<unsigned int>(guid.Data3),
		static_cast<unsigned int>(guid.Data4[0]),
		static_cast<unsigned int>(guid.Data4[1]),
		static_cast<unsigned int>(guid.Data4[2]),
		static_cast<unsigned int>(guid.Data4[3]),
		static_cast<unsigned int>(guid.Data4[4]),
		static_cast<unsigned int>(guid.Data4[5]),
		static_cast<unsigned int>(guid.Data4[6]),
		static_cast<unsigned int>(guid.Data4[7]));
	return text;
}


// The Windows message for an error code, without the trailing line end and full stop, with the code in brackets. The wide message is
// used, so a message in the system language keeps its characters.
inline std::string FormatWindowsError(DWORD code) {
	wchar_t* message = nullptr;
	DWORD length = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&message),
		0,
		nullptr);

	std::string text;
	if (length > 0 && message != nullptr) {
		std::wstring wide(message, length);
		while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L'.')) {
			wide.pop_back();
		}

		text = NarrowUtf8(wide);
	}

	if (message != nullptr) {
		LocalFree(message);
	}

	AppendFormat(text, " (error %lu)", static_cast<unsigned long>(code));
	return text;
}
