#pragma once

#include "shim/common.h"
#include <hstring.h>

namespace shim::winrt_string {

// Win7 implementation of the HSTRING subset used by Minecraft/ReUWP. Keeping
// internal callers on this namespace avoids a static RuntimeObject.lib import.
HRESULT Create(PCWSTR source, UINT32 length, HSTRING* out) noexcept;
HRESULT CreateReference(PCWSTR source, UINT32 length, HSTRING_HEADER* header,
                        HSTRING* out) noexcept;
HRESULT Duplicate(HSTRING string, HSTRING* out) noexcept;
HRESULT Delete(HSTRING string) noexcept;
PCWSTR GetRawBuffer(HSTRING string, UINT32* length) noexcept;
UINT32 GetLen(HSTRING string) noexcept;
BOOL IsEmpty(HSTRING string) noexcept;
HRESULT CompareOrdinal(HSTRING left, HSTRING right, INT32* result) noexcept;
HRESULT Concat(HSTRING left, HSTRING right, HSTRING* out) noexcept;
HRESULT GetView(HSTRING string, PCWSTR* text, UINT32* length) noexcept;
HRESULT Preallocate(UINT32 length, WCHAR** char_buffer,
                    HSTRING_BUFFER* buffer_handle) noexcept;
HRESULT Promote(HSTRING_BUFFER buffer_handle, HSTRING* out) noexcept;
HRESULT DeleteBuffer(HSTRING_BUFFER buffer_handle) noexcept;
HRESULT HasEmbeddedNull(HSTRING string, BOOL* has_embedded_null) noexcept;
HRESULT Substring(HSTRING string, UINT32 start_index, HSTRING* out) noexcept;
HRESULT SubstringWithLength(HSTRING string, UINT32 start_index, UINT32 length,
                            HSTRING* out) noexcept;
HRESULT Replace(HSTRING string, HSTRING replaced, HSTRING replacement,
                HSTRING* out) noexcept;
HRESULT TrimStart(HSTRING string, HSTRING trim_string, HSTRING* out) noexcept;
HRESULT TrimEnd(HSTRING string, HSTRING trim_string, HSTRING* out) noexcept;

}  // namespace shim::winrt_string
