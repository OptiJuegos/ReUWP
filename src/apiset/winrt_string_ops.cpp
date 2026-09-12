#include "shim/winrt_string_compat.h"

#include <cstring>
#include <limits>

namespace shim::winrt_string {
namespace {

constexpr uint32_t kBufferMagic = 0x48534257u;  // "WBSH"
constexpr HRESULT kBoundsError = static_cast<HRESULT>(0x8000000Bu);
constexpr HRESULT kInvalidSize = static_cast<HRESULT>(0x80080011u);

struct MutableBuffer {
  uint32_t magic;
  UINT32 length;
  wchar_t text[1];
};

bool Contains(PCWSTR chars, UINT32 length, wchar_t value) noexcept {
  for (UINT32 i = 0; i < length; ++i) {
    if (chars[i] == value) {
      return true;
    }
  }
  return false;
}

HRESULT CopyRange(PCWSTR text, UINT32 source_length, UINT32 start,
                  UINT32 length, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;
  if (start > source_length || length > source_length - start) {
    return kBoundsError;
  }
  if (length == 0) {
    return S_OK;
  }
  return Create(text + start, length, out);
}

}  // namespace

HRESULT Preallocate(UINT32 length, WCHAR** char_buffer,
                    HSTRING_BUFFER* buffer_handle) noexcept {
  if (char_buffer == nullptr || buffer_handle == nullptr) {
    return E_POINTER;
  }
  *char_buffer = nullptr;
  *buffer_handle = nullptr;
  if (length == 0) {
    return S_OK;
  }

  const ULONGLONG bytes64 =
      static_cast<ULONGLONG>(offsetof(MutableBuffer, text)) +
      (static_cast<ULONGLONG>(length) + 1u) * sizeof(wchar_t);
  if (bytes64 > std::numeric_limits<SIZE_T>::max()) {
    return kInvalidSize;
  }

  auto* const buffer = static_cast<MutableBuffer*>(
      ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY,
                  static_cast<SIZE_T>(bytes64)));
  if (buffer == nullptr) {
    return E_OUTOFMEMORY;
  }
  buffer->magic = kBufferMagic;
  buffer->length = length;
  buffer->text[length] = L'\0';
  *char_buffer = buffer->text;
  *buffer_handle = reinterpret_cast<HSTRING_BUFFER>(buffer);
  return S_OK;
}

HRESULT Promote(HSTRING_BUFFER buffer_handle, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  if (buffer_handle == nullptr) {
    return E_INVALIDARG;
  }

  auto* const buffer = reinterpret_cast<MutableBuffer*>(buffer_handle);
  if (buffer->magic != kBufferMagic || buffer->text[buffer->length] != L'\0') {
    return E_INVALIDARG;
  }

  const HRESULT result = Create(buffer->text, buffer->length, out);
  if (SUCCEEDED(result)) {
    buffer->magic = 0;
    ::HeapFree(::GetProcessHeap(), 0, buffer);
  }
  return result;
}

HRESULT DeleteBuffer(HSTRING_BUFFER buffer_handle) noexcept {
  if (buffer_handle == nullptr) {
    return E_POINTER;
  }
  auto* const buffer = reinterpret_cast<MutableBuffer*>(buffer_handle);
  if (buffer->magic != kBufferMagic) {
    return E_INVALIDARG;
  }
  buffer->magic = 0;
  ::HeapFree(::GetProcessHeap(), 0, buffer);
  return S_OK;
}

HRESULT HasEmbeddedNull(HSTRING string, BOOL* has_embedded_null) noexcept {
  if (has_embedded_null == nullptr) {
    return E_INVALIDARG;
  }
  *has_embedded_null = FALSE;
  PCWSTR text = nullptr;
  UINT32 length = 0;
  const HRESULT result = GetView(string, &text, &length);
  if (FAILED(result)) {
    return result;
  }
  for (UINT32 i = 0; i < length; ++i) {
    if (text[i] == L'\0') {
      *has_embedded_null = TRUE;
      break;
    }
  }
  return S_OK;
}

HRESULT Substring(HSTRING string, UINT32 start_index, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  PCWSTR text = nullptr;
  UINT32 length = 0;
  const HRESULT result = GetView(string, &text, &length);
  if (FAILED(result)) {
    *out = nullptr;
    return result;
  }
  if (start_index > length) {
    *out = nullptr;
    return kBoundsError;
  }
  return CopyRange(text, length, start_index, length - start_index, out);
}

HRESULT SubstringWithLength(HSTRING string, UINT32 start_index, UINT32 length,
                            HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;
  if (start_index > std::numeric_limits<UINT32>::max() - length) {
    return E_INVALIDARG;
  }
  PCWSTR text = nullptr;
  UINT32 source_length = 0;
  const HRESULT result = GetView(string, &text, &source_length);
  if (FAILED(result)) {
    return result;
  }
  return CopyRange(text, source_length, start_index, length, out);
}

HRESULT Replace(HSTRING string, HSTRING replaced, HSTRING replacement,
                HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;

  PCWSTR source_text = nullptr;
  PCWSTR old_text = nullptr;
  PCWSTR new_text = nullptr;
  UINT32 source_length = 0;
  UINT32 old_length = 0;
  UINT32 new_length = 0;
  HRESULT result = GetView(string, &source_text, &source_length);
  if (FAILED(result)) {
    return result;
  }
  result = GetView(replaced, &old_text, &old_length);
  if (FAILED(result) || old_length == 0) {
    return E_INVALIDARG;
  }
  result = GetView(replacement, &new_text, &new_length);
  if (FAILED(result)) {
    return result;
  }

  UINT32 matches = 0;
  if (old_length <= source_length) {
    for (UINT32 i = 0; i <= source_length - old_length;) {
      if (std::memcmp(source_text + i, old_text,
                      static_cast<size_t>(old_length) * sizeof(wchar_t)) == 0) {
        ++matches;
        i += old_length;
      } else {
        ++i;
      }
    }
  }
  if (matches == 0) {
    return Duplicate(string, out);
  }

  const LONGLONG delta = static_cast<LONGLONG>(new_length) - old_length;
  const LONGLONG final64 = static_cast<LONGLONG>(source_length) +
                           delta * static_cast<LONGLONG>(matches);
  if (final64 < 0 ||
      static_cast<ULONGLONG>(final64) >
          std::numeric_limits<UINT32>::max()) {
    return E_INVALIDARG;
  }
  const UINT32 final_length = static_cast<UINT32>(final64);
  if (final_length == 0) {
    return S_OK;
  }

  const ULONGLONG bytes64 =
      (static_cast<ULONGLONG>(final_length) + 1u) * sizeof(wchar_t);
  if (bytes64 > std::numeric_limits<SIZE_T>::max()) {
    return E_OUTOFMEMORY;
  }
  auto* const temporary = static_cast<wchar_t*>(
      ::HeapAlloc(::GetProcessHeap(), 0, static_cast<SIZE_T>(bytes64)));
  if (temporary == nullptr) {
    return E_OUTOFMEMORY;
  }

  UINT32 source_index = 0;
  UINT32 destination_index = 0;
  while (source_index < source_length) {
    const bool match = old_length <= source_length - source_index &&
                       std::memcmp(source_text + source_index, old_text,
                                   static_cast<size_t>(old_length) *
                                       sizeof(wchar_t)) == 0;
    if (match) {
      if (new_length != 0) {
        std::memcpy(temporary + destination_index, new_text,
                    static_cast<size_t>(new_length) * sizeof(wchar_t));
      }
      destination_index += new_length;
      source_index += old_length;
    } else {
      temporary[destination_index++] = source_text[source_index++];
    }
  }
  temporary[final_length] = L'\0';
  result = Create(temporary, final_length, out);
  ::HeapFree(::GetProcessHeap(), 0, temporary);
  return result;
}

HRESULT TrimStart(HSTRING string, HSTRING trim_string, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;
  PCWSTR text = nullptr;
  PCWSTR trim = nullptr;
  UINT32 length = 0;
  UINT32 trim_length = 0;
  HRESULT result = GetView(string, &text, &length);
  if (FAILED(result)) {
    return result;
  }
  result = GetView(trim_string, &trim, &trim_length);
  if (FAILED(result) || trim_length == 0) {
    return E_INVALIDARG;
  }

  UINT32 start = 0;
  while (start < length && Contains(trim, trim_length, text[start])) {
    ++start;
  }
  if (start == 0) {
    return Duplicate(string, out);
  }
  return CopyRange(text, length, start, length - start, out);
}

HRESULT TrimEnd(HSTRING string, HSTRING trim_string, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_INVALIDARG;
  }
  *out = nullptr;
  PCWSTR text = nullptr;
  PCWSTR trim = nullptr;
  UINT32 length = 0;
  UINT32 trim_length = 0;
  HRESULT result = GetView(string, &text, &length);
  if (FAILED(result)) {
    return result;
  }
  result = GetView(trim_string, &trim, &trim_length);
  if (FAILED(result) || trim_length == 0) {
    return E_INVALIDARG;
  }

  UINT32 end = length;
  while (end != 0 && Contains(trim, trim_length, text[end - 1])) {
    --end;
  }
  if (end == length) {
    return Duplicate(string, out);
  }
  return CopyRange(text, length, 0, end, out);
}

}  // namespace shim::winrt_string

extern "C" {

HRESULT SHIM_COM WindowsPreallocateStringBuffer(
    UINT32 length, WCHAR** char_buffer, HSTRING_BUFFER* buffer_handle) {
  return shim::winrt_string::Preallocate(length, char_buffer, buffer_handle);
}

HRESULT SHIM_COM WindowsPromoteStringBuffer(HSTRING_BUFFER buffer_handle,
                                            HSTRING* out) {
  return shim::winrt_string::Promote(buffer_handle, out);
}

HRESULT SHIM_COM WindowsDeleteStringBuffer(HSTRING_BUFFER buffer_handle) {
  return shim::winrt_string::DeleteBuffer(buffer_handle);
}

HRESULT SHIM_COM WindowsStringHasEmbeddedNull(HSTRING string,
                                               BOOL* has_embedded_null) {
  return shim::winrt_string::HasEmbeddedNull(string, has_embedded_null);
}

HRESULT SHIM_COM WindowsSubstring(HSTRING string, UINT32 start_index,
                                  HSTRING* out) {
  return shim::winrt_string::Substring(string, start_index, out);
}

HRESULT SHIM_COM WindowsSubstringWithSpecifiedLength(HSTRING string,
                                                      UINT32 start_index,
                                                      UINT32 length,
                                                      HSTRING* out) {
  return shim::winrt_string::SubstringWithLength(string, start_index, length,
                                                  out);
}

HRESULT SHIM_COM WindowsReplaceString(HSTRING string, HSTRING replaced,
                                      HSTRING replacement, HSTRING* out) {
  return shim::winrt_string::Replace(string, replaced, replacement, out);
}

HRESULT SHIM_COM WindowsTrimStringStart(HSTRING string, HSTRING trim_string,
                                        HSTRING* out) {
  return shim::winrt_string::TrimStart(string, trim_string, out);
}

HRESULT SHIM_COM WindowsTrimStringEnd(HSTRING string, HSTRING trim_string,
                                      HSTRING* out) {
  return shim::winrt_string::TrimEnd(string, trim_string, out);
}

}  // extern "C"
