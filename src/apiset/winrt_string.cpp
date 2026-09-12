#include "shim/winrt_string_compat.h"

#include <cstring>
#include <limits>

namespace shim::winrt_string {
namespace {

constexpr uint32_t kOwnedMagic = 0x48534F57u;      // "WOSH"
constexpr uint32_t kReferenceMagic = 0x48535257u;  // "WRSH"
constexpr wchar_t kEmptyString[] = L"";

struct OwnedString {
  uint32_t magic;
  volatile LONG refs;
  UINT32 length;
  wchar_t text[1];
};

// Stored directly in caller-owned HSTRING_HEADER bytes. x86 HSTRING_HEADER has
// 20 reserved bytes; this representation deliberately needs only 12.
struct ReferenceString {
  uint32_t magic;
  UINT32 length;
  PCWSTR text;
};

static_assert(sizeof(ReferenceString) <= sizeof(HSTRING_HEADER),
              "reference HSTRING must fit in HSTRING_HEADER");

struct View {
  PCWSTR text = kEmptyString;
  UINT32 length = 0;
  OwnedString* owned = nullptr;
  bool reference = false;
};

bool Decode(HSTRING string, View* view) noexcept {
  if (view == nullptr) {
    return false;
  }
  *view = {};
  if (string == nullptr) {
    return true;
  }

  const auto* const raw = reinterpret_cast<const uint32_t*>(string);
  const uint32_t magic = *raw;
  if (magic == kOwnedMagic) {
    auto* const owned = reinterpret_cast<OwnedString*>(string);
    view->text = owned->text;
    view->length = owned->length;
    view->owned = owned;
    return true;
  }
  if (magic == kReferenceMagic) {
    ReferenceString reference = {};
    std::memcpy(&reference, reinterpret_cast<const void*>(string),
                sizeof(reference));
    view->text = reference.text != nullptr ? reference.text : kEmptyString;
    view->length = reference.length;
    view->reference = true;
    return true;
  }
  return false;
}

HRESULT AllocateOwned(PCWSTR first, UINT32 first_length, PCWSTR second,
                      UINT32 second_length, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;

  const ULONGLONG total64 = static_cast<ULONGLONG>(first_length) +
                            static_cast<ULONGLONG>(second_length);
  if (total64 > std::numeric_limits<UINT32>::max()) {
    return E_INVALIDARG;
  }
  const UINT32 total = static_cast<UINT32>(total64);
  if (total == 0) {
    return S_OK;
  }

  const size_t prefix = offsetof(OwnedString, text);
  const ULONGLONG bytes64 = static_cast<ULONGLONG>(prefix) +
                            (static_cast<ULONGLONG>(total) + 1u) *
                                sizeof(wchar_t);
  if (bytes64 > std::numeric_limits<SIZE_T>::max()) {
    return E_OUTOFMEMORY;
  }

  auto* const owned = static_cast<OwnedString*>(
      ::HeapAlloc(::GetProcessHeap(), 0, static_cast<SIZE_T>(bytes64)));
  if (owned == nullptr) {
    return E_OUTOFMEMORY;
  }
  owned->magic = kOwnedMagic;
  owned->refs = 1;
  owned->length = total;
  if (first_length != 0) {
    std::memcpy(owned->text, first,
                static_cast<size_t>(first_length) * sizeof(wchar_t));
  }
  if (second_length != 0) {
    std::memcpy(owned->text + first_length, second,
                static_cast<size_t>(second_length) * sizeof(wchar_t));
  }
  owned->text[total] = L'\0';
  *out = reinterpret_cast<HSTRING>(owned);
  return S_OK;
}

}  // namespace

HRESULT Create(PCWSTR source, UINT32 length, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;
  if (source == nullptr) {
    return length == 0 ? S_OK : E_POINTER;
  }
  return AllocateOwned(source, length, nullptr, 0, out);
}

HRESULT CreateReference(PCWSTR source, UINT32 length, HSTRING_HEADER* header,
                        HSTRING* out) noexcept {
  if (header == nullptr || out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;
  std::memset(header, 0, sizeof(*header));
  if (source == nullptr) {
    return length == 0 ? S_OK : E_POINTER;
  }
  if (length == 0) {
    return S_OK;
  }
  if (source[length] != L'\0') {
    return E_INVALIDARG;
  }

  const ReferenceString reference = {kReferenceMagic, length, source};
  std::memcpy(header, &reference, sizeof(reference));
  *out = reinterpret_cast<HSTRING>(header);
  return S_OK;
}

HRESULT Duplicate(HSTRING string, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;
  View view;
  if (!Decode(string, &view)) {
    return E_INVALIDARG;
  }
  if (string == nullptr || view.length == 0) {
    return S_OK;
  }
  if (view.owned != nullptr) {
    ::InterlockedIncrement(&view.owned->refs);
    *out = string;
    return S_OK;
  }
  // Fast-pass strings cannot transfer the caller's stack lifetime. Duplicate
  // therefore takes an owned copy, matching Windows Runtime semantics.
  return AllocateOwned(view.text, view.length, nullptr, 0, out);
}

HRESULT Delete(HSTRING string) noexcept {
  if (string == nullptr) {
    return S_OK;
  }
  View view;
  if (!Decode(string, &view)) {
    // The documented API always returns S_OK; do not free an unknown handle.
    return S_OK;
  }
  if (view.owned != nullptr && ::InterlockedDecrement(&view.owned->refs) == 0) {
    view.owned->magic = 0;
    ::HeapFree(::GetProcessHeap(), 0, view.owned);
  }
  return S_OK;
}

PCWSTR GetRawBuffer(HSTRING string, UINT32* length) noexcept {
  View view;
  if (!Decode(string, &view)) {
    if (length != nullptr) {
      *length = 0;
    }
    return kEmptyString;
  }
  if (length != nullptr) {
    *length = view.length;
  }
  return view.length == 0 ? kEmptyString : view.text;
}

UINT32 GetLen(HSTRING string) noexcept {
  View view;
  return Decode(string, &view) ? view.length : 0;
}

BOOL IsEmpty(HSTRING string) noexcept {
  return GetLen(string) == 0 ? TRUE : FALSE;
}

HRESULT CompareOrdinal(HSTRING left, HSTRING right, INT32* result) noexcept {
  if (result == nullptr) {
    return E_INVALIDARG;
  }
  View a;
  View b;
  if (!Decode(left, &a) || !Decode(right, &b)) {
    *result = 0;
    return E_INVALIDARG;
  }
  const UINT32 shared = a.length < b.length ? a.length : b.length;
  for (UINT32 i = 0; i < shared; ++i) {
    if (a.text[i] < b.text[i]) {
      *result = -1;
      return S_OK;
    }
    if (a.text[i] > b.text[i]) {
      *result = 1;
      return S_OK;
    }
  }
  *result = a.length < b.length ? -1 : (a.length > b.length ? 1 : 0);
  return S_OK;
}

HRESULT Concat(HSTRING left, HSTRING right, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  View a;
  View b;
  if (!Decode(left, &a) || !Decode(right, &b)) {
    *out = nullptr;
    return E_INVALIDARG;
  }
  if (a.length == 0) {
    return Duplicate(right, out);
  }
  if (b.length == 0) {
    return Duplicate(left, out);
  }
  return AllocateOwned(a.text, a.length, b.text, b.length, out);
}

HRESULT GetView(HSTRING string, PCWSTR* text, UINT32* length) noexcept {
  if (text == nullptr || length == nullptr) {
    return E_POINTER;
  }
  View view;
  if (!Decode(string, &view)) {
    *text = kEmptyString;
    *length = 0;
    return E_INVALIDARG;
  }
  *text = view.text;
  *length = view.length;
  return S_OK;
}

}  // namespace shim::winrt_string

// Export names intentionally match RuntimeObject/ComBase. This translation
// unit includes hstring.h for types but not winstring.h, so the SDK does not
// mark these definitions dllimport.
extern "C" {

HRESULT SHIM_COM WindowsCreateString(PCWSTR source, UINT32 length,
                                     HSTRING* out) {
  return shim::winrt_string::Create(source, length, out);
}

HRESULT SHIM_COM WindowsCreateStringReference(PCWSTR source, UINT32 length,
                                              HSTRING_HEADER* header,
                                              HSTRING* out) {
  return shim::winrt_string::CreateReference(source, length, header, out);
}

HRESULT SHIM_COM WindowsDuplicateString(HSTRING string, HSTRING* out) {
  return shim::winrt_string::Duplicate(string, out);
}

HRESULT SHIM_COM WindowsDeleteString(HSTRING string) {
  return shim::winrt_string::Delete(string);
}

PCWSTR SHIM_COM WindowsGetStringRawBuffer(HSTRING string, UINT32* length) {
  return shim::winrt_string::GetRawBuffer(string, length);
}

UINT32 SHIM_COM WindowsGetStringLen(HSTRING string) {
  return shim::winrt_string::GetLen(string);
}

BOOL SHIM_COM WindowsIsStringEmpty(HSTRING string) {
  return shim::winrt_string::IsEmpty(string);
}

HRESULT SHIM_COM WindowsCompareStringOrdinal(HSTRING left, HSTRING right,
                                             INT32* result) {
  return shim::winrt_string::CompareOrdinal(left, right, result);
}

HRESULT SHIM_COM WindowsConcatString(HSTRING left, HSTRING right,
                                     HSTRING* out) {
  return shim::winrt_string::Concat(left, right, out);
}

}  // extern "C"
