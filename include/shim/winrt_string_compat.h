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

}  // namespace shim::winrt_string
