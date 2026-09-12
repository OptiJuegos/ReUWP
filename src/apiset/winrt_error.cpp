#include "shim/winrt_error_compat.h"

#include "shim/winrt_string_compat.h"

#include <hstring.h>

namespace {

constexpr UINT32 kMaxMessageChars = 511;

struct ErrorMessage {
  wchar_t text[kMaxMessageChars + 1] = {};
  bool should_store = false;
  bool caller_supplied = false;
};

void CopyMessage(PCWSTR source, UINT32 length, ErrorMessage* message) noexcept {
  if (message == nullptr || source == nullptr || length == 0) {
    return;
  }

  const UINT32 capped = length < kMaxMessageChars ? length : kMaxMessageChars;
  UINT32 copied = 0;
  while (copied < capped && source[copied] != L'\0') {
    message->text[copied] = source[copied];
    ++copied;
  }
  message->text[copied] = L'\0';
  message->caller_supplied = copied != 0;
}

void FormatFallback(HRESULT error, ErrorMessage* message) noexcept {
  if (message == nullptr || message->text[0] != L'\0') {
    return;
  }

  DWORD length = ::FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
      static_cast<DWORD>(error), 0, message->text, kMaxMessageChars + 1,
      nullptr);
  while (length != 0 &&
         (message->text[length - 1] == L'\r' ||
          message->text[length - 1] == L'\n')) {
    message->text[--length] = L'\0';
  }
  if (length == 0) {
    ::lstrcpynW(message->text, L"Unspecified Windows Runtime error",
                kMaxMessageChars + 1);
  }
}

ErrorMessage ReadMessage(HRESULT error, HSTRING string) noexcept {
  ErrorMessage message;
  if (string == nullptr) {
    FormatFallback(error, &message);
    message.should_store = true;
    return message;
  }

  UINT32 length = 0;
  PCWSTR const raw = shim::winrt_string::GetRawBuffer(string, &length);
  CopyMessage(raw, length, &message);
  message.should_store = message.caller_supplied;
  return message;
}

HRESULT Originate(HRESULT error, HSTRING message, IUnknown* language_exception,
                  bool allow_success, bool* reported) noexcept {
  if (reported != nullptr) {
    *reported = false;
  }
  if (!allow_success && SUCCEEDED(error)) {
    return S_FALSE;
  }

  const ErrorMessage text = ReadMessage(error, message);
  if (!text.should_store) {
    return S_FALSE;
  }

  IUnknown* restricted_error = nullptr;
  HRESULT hr = shim::winrt_error::CreateRestrictedErrorInfo(
      error, text.text, language_exception, &restricted_error);
  if (FAILED(hr)) {
    return hr;
  }

  hr = shim::winrt_error::SetThreadError(restricted_error);
  if (SUCCEEDED(hr)) {
    shim::winrt_error::ReportToDebugger(restricted_error);
    if (reported != nullptr) {
      *reported = text.caller_supplied;
    }
  }
  restricted_error->Release();
  return hr;
}

}  // namespace

extern "C" {

HRESULT SHIM_COM SetRestrictedErrorInfo(IUnknown* restricted_error) {
  return shim::winrt_error::SetThreadError(restricted_error);
}

HRESULT SHIM_COM GetRestrictedErrorInfo(IUnknown** restricted_error) {
  return shim::winrt_error::TakeThreadError(restricted_error);
}

BOOL SHIM_COM RoOriginateError(HRESULT error, HSTRING message) {
  bool reported = false;
  const HRESULT hr = Originate(error, message, nullptr, false, &reported);
  return SUCCEEDED(hr) && reported ? TRUE : FALSE;
}

BOOL SHIM_COM RoOriginateLanguageException(HRESULT error, HSTRING message,
                                           IUnknown* language_exception) {
  bool reported = false;
  const HRESULT hr =
      Originate(error, message, language_exception, false, &reported);
  return SUCCEEDED(hr) && reported ? TRUE : FALSE;
}

BOOL SHIM_COM RoTransformError(HRESULT old_error, HRESULT new_error,
                               HSTRING message) {
  if (old_error == new_error ||
      (SUCCEEDED(old_error) && SUCCEEDED(new_error))) {
    return FALSE;
  }

  const ErrorMessage text = ReadMessage(new_error, message);
  if (!text.should_store) {
    return FALSE;
  }

  IUnknown* previous = nullptr;
  IUnknown* language_exception = nullptr;
  if (shim::winrt_error::TakeThreadError(&previous) == S_OK) {
    HRESULT previous_error = S_OK;
    if (SUCCEEDED(shim::winrt_error::GetErrorCode(previous, &previous_error)) &&
        previous_error == old_error) {
      shim::winrt_error::GetLanguageException(previous, &language_exception);
    } else {
      shim::winrt_error::SetThreadError(previous);
    }
    previous->Release();
  }

  bool reported = false;
  const HRESULT hr =
      Originate(new_error, message, language_exception, true, &reported);
  if (language_exception != nullptr) {
    language_exception->Release();
  }
  return SUCCEEDED(hr) && reported ? TRUE : FALSE;
}

HRESULT SHIM_COM RoCaptureErrorContext(HRESULT error) {
  if (SUCCEEDED(error)) {
    return S_OK;
  }

  IUnknown* current = nullptr;
  if (shim::winrt_error::TakeThreadError(&current) == S_OK) {
    HRESULT current_error = S_OK;
    const HRESULT details_hr =
        shim::winrt_error::GetErrorCode(current, &current_error);
    if (SUCCEEDED(details_hr) && current_error == error) {
      const HRESULT restore_hr = shim::winrt_error::SetThreadError(current);
      current->Release();
      return restore_hr;
    }
    current->Release();
  }

  bool ignored = false;
  const HRESULT originate_hr =
      Originate(error, nullptr, nullptr, false, &ignored);
  return originate_hr == S_FALSE ? S_OK : originate_hr;
}

HRESULT SHIM_COM RoReportUnhandledError(IUnknown* restricted_error) {
  if (restricted_error == nullptr) {
    return E_INVALIDARG;
  }

  HRESULT error = S_OK;
  const HRESULT hr = shim::winrt_error::GetErrorCode(restricted_error, &error);
  if (FAILED(hr)) {
    return hr;
  }

  shim::winrt_error::ReportToDebugger(restricted_error);
  return S_OK;
}

void SHIM_COM RoFailFastWithErrorContext(HRESULT error) {
  ::RaiseException(static_cast<DWORD>(error), EXCEPTION_NONCONTINUABLE, 0,
                   nullptr);
  ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(error));
}

}  // extern "C"
