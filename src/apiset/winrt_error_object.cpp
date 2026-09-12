#include "shim/winrt_error_compat.h"

#include <oaidl.h>
#include <oleauto.h>
#include <stddef.h>

namespace shim::winrt_error {
namespace {

constexpr GUID kIidIUnknown = {0x00000000,
                               0x0000,
                               0x0000,
                               {0xC0, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x46}};
constexpr GUID kIidIErrorInfo = {0x1CF2B120,
                                 0x547D,
                                 0x101B,
                                 {0x8E, 0x65, 0x08, 0x00,
                                  0x2B, 0x2B, 0xD1, 0x19}};
constexpr GUID kIidIRestrictedErrorInfo = {0x82BA7092,
                                           0x4C88,
                                           0x427D,
                                           {0xA7, 0xBC, 0x16, 0xDD,
                                            0x93, 0xFE, 0xB6, 0x7E}};
constexpr GUID kIidILanguageExceptionErrorInfo = {
    0x04A2DBF3,
    0xDF83,
    0x116C,
    {0x09, 0x46, 0x08, 0x12, 0xAB, 0xF6, 0xE0, 0x7D}};
constexpr GUID kIidIAgileObject = {0x94EA2B94,
                                   0xE9CC,
                                   0x49E0,
                                   {0xC0, 0xFF, 0xEE, 0x64,
                                    0xCA, 0x8F, 0x5B, 0x90}};

struct IRestrictedErrorInfoCompat;
struct IErrorInfoCompat;
struct ILanguageExceptionErrorInfoCompat;

struct RestrictedErrorInfoVtbl {
  HRESULT(SHIM_COM* query_interface)(IRestrictedErrorInfoCompat*, REFIID,
                                     void**);
  ULONG(SHIM_COM* add_ref)(IRestrictedErrorInfoCompat*);
  ULONG(SHIM_COM* release)(IRestrictedErrorInfoCompat*);
  HRESULT(SHIM_COM* get_error_details)(IRestrictedErrorInfoCompat*, BSTR*,
                                       HRESULT*, BSTR*, BSTR*);
  HRESULT(SHIM_COM* get_reference)(IRestrictedErrorInfoCompat*, BSTR*);
};

struct IRestrictedErrorInfoCompat {
  const RestrictedErrorInfoVtbl* lpVtbl;
};

struct ErrorInfoVtbl {
  HRESULT(SHIM_COM* query_interface)(IErrorInfoCompat*, REFIID, void**);
  ULONG(SHIM_COM* add_ref)(IErrorInfoCompat*);
  ULONG(SHIM_COM* release)(IErrorInfoCompat*);
  HRESULT(SHIM_COM* get_guid)(IErrorInfoCompat*, GUID*);
  HRESULT(SHIM_COM* get_source)(IErrorInfoCompat*, BSTR*);
  HRESULT(SHIM_COM* get_description)(IErrorInfoCompat*, BSTR*);
  HRESULT(SHIM_COM* get_help_file)(IErrorInfoCompat*, BSTR*);
  HRESULT(SHIM_COM* get_help_context)(IErrorInfoCompat*, DWORD*);
};

struct IErrorInfoCompat {
  const ErrorInfoVtbl* lpVtbl;
};

struct LanguageExceptionErrorInfoVtbl {
  HRESULT(SHIM_COM* query_interface)(ILanguageExceptionErrorInfoCompat*,
                                     REFIID, void**);
  ULONG(SHIM_COM* add_ref)(ILanguageExceptionErrorInfoCompat*);
  ULONG(SHIM_COM* release)(ILanguageExceptionErrorInfoCompat*);
  HRESULT(SHIM_COM* get_language_exception)(
      ILanguageExceptionErrorInfoCompat*, IUnknown**);
};

struct ILanguageExceptionErrorInfoCompat {
  const LanguageExceptionErrorInfoVtbl* lpVtbl;
};

struct RestrictedErrorObject {
  IRestrictedErrorInfoCompat restricted;
  IErrorInfoCompat error_info;
  ILanguageExceptionErrorInfoCompat language;
  volatile LONG refs;
  HRESULT error;
  BSTR description;
  BSTR restricted_description;
  IUnknown* language_exception;
};

RestrictedErrorObject* FromRestricted(IRestrictedErrorInfoCompat* iface) {
  return reinterpret_cast<RestrictedErrorObject*>(
      reinterpret_cast<unsigned char*>(iface) -
      offsetof(RestrictedErrorObject, restricted));
}

RestrictedErrorObject* FromErrorInfo(IErrorInfoCompat* iface) {
  return reinterpret_cast<RestrictedErrorObject*>(
      reinterpret_cast<unsigned char*>(iface) -
      offsetof(RestrictedErrorObject, error_info));
}

RestrictedErrorObject* FromLanguage(ILanguageExceptionErrorInfoCompat* iface) {
  return reinterpret_cast<RestrictedErrorObject*>(
      reinterpret_cast<unsigned char*>(iface) -
      offsetof(RestrictedErrorObject, language));
}

ULONG AddRef(RestrictedErrorObject* object) {
  return static_cast<ULONG>(::InterlockedIncrement(&object->refs));
}

ULONG Release(RestrictedErrorObject* object) {
  const LONG refs = ::InterlockedDecrement(&object->refs);
  if (refs != 0) {
    return static_cast<ULONG>(refs);
  }

  if (object->language_exception != nullptr) {
    object->language_exception->Release();
  }
  ::SysFreeString(object->description);
  ::SysFreeString(object->restricted_description);
  ::HeapFree(::GetProcessHeap(), 0, object);
  return 0;
}

HRESULT QueryInterface(RestrictedErrorObject* object, REFIID iid, void** out) {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;

  if (::IsEqualGUID(iid, kIidIUnknown) ||
      ::IsEqualGUID(iid, kIidIRestrictedErrorInfo) ||
      ::IsEqualGUID(iid, kIidIAgileObject)) {
    *out = &object->restricted;
  } else if (::IsEqualGUID(iid, kIidIErrorInfo)) {
    *out = &object->error_info;
  } else if (::IsEqualGUID(iid, kIidILanguageExceptionErrorInfo) &&
             object->language_exception != nullptr) {
    *out = &object->language;
  } else {
    return E_NOINTERFACE;
  }

  AddRef(object);
  return S_OK;
}

HRESULT SHIM_COM RestrictedQueryInterface(IRestrictedErrorInfoCompat* self,
                                          REFIID iid, void** out) {
  return QueryInterface(FromRestricted(self), iid, out);
}

ULONG SHIM_COM RestrictedAddRef(IRestrictedErrorInfoCompat* self) {
  return AddRef(FromRestricted(self));
}

ULONG SHIM_COM RestrictedRelease(IRestrictedErrorInfoCompat* self) {
  return Release(FromRestricted(self));
}

HRESULT DuplicateBstr(BSTR source, BSTR* out) {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  const UINT length = source != nullptr ? ::SysStringLen(source) : 0;
  *out = ::SysAllocStringLen(source, length);
  return *out != nullptr || length == 0 ? S_OK : E_OUTOFMEMORY;
}

HRESULT SHIM_COM RestrictedGetErrorDetails(IRestrictedErrorInfoCompat* self,
                                           BSTR* description, HRESULT* error,
                                           BSTR* restricted_description,
                                           BSTR* capability_sid) {
  if (description == nullptr || error == nullptr ||
      restricted_description == nullptr || capability_sid == nullptr) {
    return E_POINTER;
  }

  *description = nullptr;
  *restricted_description = nullptr;
  *capability_sid = nullptr;
  RestrictedErrorObject* const object = FromRestricted(self);
  *error = object->error;

  HRESULT hr = DuplicateBstr(object->description, description);
  if (FAILED(hr)) {
    return hr;
  }
  hr = DuplicateBstr(object->restricted_description, restricted_description);
  if (FAILED(hr)) {
    ::SysFreeString(*description);
    *description = nullptr;
    return hr;
  }
  *capability_sid = ::SysAllocStringLen(nullptr, 0);
  return S_OK;
}

HRESULT SHIM_COM RestrictedGetReference(IRestrictedErrorInfoCompat*,
                                        BSTR* reference) {
  if (reference == nullptr) {
    return E_POINTER;
  }
  *reference = ::SysAllocStringLen(nullptr, 0);
  return S_OK;
}

HRESULT SHIM_COM ErrorQueryInterface(IErrorInfoCompat* self, REFIID iid, void** out) {
  return QueryInterface(FromErrorInfo(self), iid, out);
}

ULONG SHIM_COM ErrorAddRef(IErrorInfoCompat* self) {
  return AddRef(FromErrorInfo(self));
}

ULONG SHIM_COM ErrorRelease(IErrorInfoCompat* self) {
  return Release(FromErrorInfo(self));
}

HRESULT SHIM_COM ErrorGetGuid(IErrorInfoCompat*, GUID* guid) {
  if (guid == nullptr) {
    return E_POINTER;
  }
  *guid = GUID_NULL;
  return S_OK;
}

HRESULT SHIM_COM ErrorGetSource(IErrorInfoCompat*, BSTR* source) {
  if (source == nullptr) {
    return E_POINTER;
  }
  *source = ::SysAllocString(L"ReUWP");
  return *source != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT SHIM_COM ErrorGetDescription(IErrorInfoCompat* self, BSTR* description) {
  return DuplicateBstr(FromErrorInfo(self)->description, description);
}

HRESULT SHIM_COM ErrorGetHelpFile(IErrorInfoCompat*, BSTR* help_file) {
  if (help_file == nullptr) {
    return E_POINTER;
  }
  *help_file = ::SysAllocStringLen(nullptr, 0);
  return S_OK;
}

HRESULT SHIM_COM ErrorGetHelpContext(IErrorInfoCompat*, DWORD* help_context) {
  if (help_context == nullptr) {
    return E_POINTER;
  }
  *help_context = 0;
  return S_OK;
}

HRESULT SHIM_COM LanguageQueryInterface(ILanguageExceptionErrorInfoCompat* self,
                                        REFIID iid, void** out) {
  return QueryInterface(FromLanguage(self), iid, out);
}

ULONG SHIM_COM LanguageAddRef(ILanguageExceptionErrorInfoCompat* self) {
  return AddRef(FromLanguage(self));
}

ULONG SHIM_COM LanguageRelease(ILanguageExceptionErrorInfoCompat* self) {
  return Release(FromLanguage(self));
}

HRESULT SHIM_COM LanguageGetException(ILanguageExceptionErrorInfoCompat* self,
                                      IUnknown** exception) {
  if (exception == nullptr) {
    return E_POINTER;
  }
  *exception = nullptr;
  IUnknown* const stored = FromLanguage(self)->language_exception;
  if (stored == nullptr) {
    return S_FALSE;
  }
  stored->AddRef();
  *exception = stored;
  return S_OK;
}

const RestrictedErrorInfoVtbl kRestrictedVtbl = {
    RestrictedQueryInterface, RestrictedAddRef, RestrictedRelease,
    RestrictedGetErrorDetails, RestrictedGetReference};

const ErrorInfoVtbl kErrorInfoVtbl = {
    ErrorQueryInterface, ErrorAddRef, ErrorRelease, ErrorGetGuid,
    ErrorGetSource,      ErrorGetDescription, ErrorGetHelpFile,
    ErrorGetHelpContext};

const LanguageExceptionErrorInfoVtbl kLanguageVtbl = {
    LanguageQueryInterface, LanguageAddRef, LanguageRelease,
    LanguageGetException};

}  // namespace

HRESULT CreateRestrictedErrorInfo(HRESULT error, PCWSTR message,
                                  IUnknown* language_exception,
                                  IUnknown** restricted_error) noexcept {
  if (restricted_error == nullptr) {
    return E_POINTER;
  }
  *restricted_error = nullptr;

  auto* const object = static_cast<RestrictedErrorObject*>(
      ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY,
                  sizeof(RestrictedErrorObject)));
  if (object == nullptr) {
    return E_OUTOFMEMORY;
  }

  object->restricted.lpVtbl = &kRestrictedVtbl;
  object->error_info.lpVtbl = &kErrorInfoVtbl;
  object->language.lpVtbl = &kLanguageVtbl;
  object->refs = 1;
  object->error = error;
  object->description = ::SysAllocString(message != nullptr ? message : L"");
  object->restricted_description =
      ::SysAllocString(message != nullptr ? message : L"");
  if (object->description == nullptr || object->restricted_description == nullptr) {
    Release(object);
    return E_OUTOFMEMORY;
  }

  if (language_exception != nullptr) {
    language_exception->AddRef();
    object->language_exception = language_exception;
  }

  *restricted_error = reinterpret_cast<IUnknown*>(&object->restricted);
  return S_OK;
}

HRESULT SetThreadError(IUnknown* restricted_error) noexcept {
  if (restricted_error == nullptr) {
    return ::SetErrorInfo(0, nullptr);
  }

  IErrorInfo* error_info = nullptr;
  const HRESULT hr = restricted_error->QueryInterface(
      kIidIErrorInfo, reinterpret_cast<void**>(&error_info));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT set_hr = ::SetErrorInfo(0, error_info);
  error_info->Release();
  return set_hr;
}

HRESULT TakeThreadError(IUnknown** restricted_error) noexcept {
  if (restricted_error == nullptr) {
    return E_POINTER;
  }
  *restricted_error = nullptr;

  IErrorInfo* error_info = nullptr;
  const HRESULT get_hr = ::GetErrorInfo(0, &error_info);
  if (get_hr != S_OK || error_info == nullptr) {
    return S_FALSE;
  }

  const HRESULT query_hr = error_info->QueryInterface(
      kIidIRestrictedErrorInfo, reinterpret_cast<void**>(restricted_error));
  error_info->Release();
  return SUCCEEDED(query_hr) ? S_OK : S_FALSE;
}

HRESULT GetErrorCode(IUnknown* restricted_error, HRESULT* error) noexcept {
  if (restricted_error == nullptr || error == nullptr) {
    return E_POINTER;
  }

  IRestrictedErrorInfoCompat* restricted = nullptr;
  HRESULT hr = restricted_error->QueryInterface(
      kIidIRestrictedErrorInfo, reinterpret_cast<void**>(&restricted));
  if (FAILED(hr)) {
    return hr;
  }

  BSTR description = nullptr;
  BSTR restricted_description = nullptr;
  BSTR capability_sid = nullptr;
  hr = restricted->lpVtbl->get_error_details(
      restricted, &description, error, &restricted_description, &capability_sid);
  ::SysFreeString(description);
  ::SysFreeString(restricted_description);
  ::SysFreeString(capability_sid);
  restricted->lpVtbl->release(restricted);
  return hr;
}

HRESULT GetLanguageException(IUnknown* restricted_error,
                             IUnknown** language_exception) noexcept {
  if (language_exception == nullptr) {
    return E_POINTER;
  }
  *language_exception = nullptr;
  if (restricted_error == nullptr) {
    return E_POINTER;
  }

  ILanguageExceptionErrorInfoCompat* language = nullptr;
  const HRESULT hr = restricted_error->QueryInterface(
      kIidILanguageExceptionErrorInfo, reinterpret_cast<void**>(&language));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT get_hr =
      language->lpVtbl->get_language_exception(language, language_exception);
  language->lpVtbl->release(language);
  return get_hr;
}

void ReportToDebugger(IUnknown* restricted_error) noexcept {
  if (restricted_error == nullptr || !::IsDebuggerPresent()) {
    return;
  }

  IErrorInfo* error_info = nullptr;
  if (FAILED(restricted_error->QueryInterface(
          kIidIErrorInfo, reinterpret_cast<void**>(&error_info)))) {
    return;
  }

  BSTR description = nullptr;
  if (SUCCEEDED(error_info->GetDescription(&description)) &&
      description != nullptr && description[0] != L'\0') {
    ::OutputDebugStringW(L"ReUWP WinRT error: ");
    ::OutputDebugStringW(description);
    ::OutputDebugStringW(L"\n");
  }
  ::SysFreeString(description);
  error_info->Release();
}

}  // namespace shim::winrt_error
