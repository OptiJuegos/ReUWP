#pragma once

#include "shim/common.h"

#include <unknwn.h>

namespace shim::winrt_error {

HRESULT CreateRestrictedErrorInfo(HRESULT error, PCWSTR message,
                                  IUnknown* language_exception,
                                  IUnknown** restricted_error) noexcept;

HRESULT SetThreadError(IUnknown* restricted_error) noexcept;
HRESULT TakeThreadError(IUnknown** restricted_error) noexcept;

HRESULT GetErrorCode(IUnknown* restricted_error, HRESULT* error) noexcept;
HRESULT GetLanguageException(IUnknown* restricted_error,
                             IUnknown** language_exception) noexcept;

void ReportToDebugger(IUnknown* restricted_error) noexcept;

}  // namespace shim::winrt_error
