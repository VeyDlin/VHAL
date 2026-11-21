#pragma once

#include <string>

#include <objbase.h>
#include <windows.h>

#include "WinText.h"

// Owns one COM interface pointer and releases it when it goes out of scope. Address() is for the out parameters of the COM calls.
template <typename Interface>
class ComPointer {
private:
	Interface* pointer = nullptr;


public:
	ComPointer() = default;

	explicit ComPointer(Interface* value) {
		pointer = value;
	}


	ComPointer(const ComPointer&) = delete;
	ComPointer& operator=(const ComPointer&) = delete;


	ComPointer(ComPointer&& other) noexcept {
		pointer = other.pointer;
		other.pointer = nullptr;
	}


	ComPointer& operator=(ComPointer&& other) noexcept {
		if (this != &other) {
			Reset();
			pointer = other.pointer;
			other.pointer = nullptr;
		}

		return *this;
	}


	~ComPointer() {
		Reset();
	}


	Interface* Get() const {
		return pointer;
	}


	// Releases the held pointer and returns the slot for an out parameter.
	Interface** Address() {
		Reset();
		return &pointer;
	}


	Interface* operator->() const {
		return pointer;
	}


	bool IsValid() const {
		return pointer != nullptr;
	}


	void Reset() {
		if (pointer != nullptr) {
			pointer->Release();
			pointer = nullptr;
		}
	}
};


// Initialises COM for the calling thread, and uninitialises it when it goes out of scope, only when this object initialised it.
// RPC_E_CHANGED_MODE means the thread already has COM in another threading model, which the audio calls work with, so it is usable.
class ScopedCom {
private:
	bool initialised = false;
	bool ready = false;


public:
	ScopedCom() {
		HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		initialised = SUCCEEDED(result);
		ready = initialised || result == RPC_E_CHANGED_MODE;
	}


	ScopedCom(const ScopedCom&) = delete;
	ScopedCom& operator=(const ScopedCom&) = delete;


	~ScopedCom() {
		if (initialised) {
			CoUninitialize();
		}
	}


	bool IsReady() const {
		return ready;
	}
};


// What a failed COM call says: the caller's words and the Windows message for the HRESULT.
inline std::string HresultText(const std::string& what, HRESULT result) {
	return what + ": " + FormatWindowsError(static_cast<DWORD>(result));
}
