#pragma once

#include "shim/common.h"

namespace shim::winrt {

enum class RuntimeInitType : int {
  kSingleThreaded = 0,
  kMultiThreaded = 1,
};

HRESULT InitializeApartment(RuntimeInitType type) noexcept;
void UninitializeApartment() noexcept;
HRESULT CheckApartmentInitialized() noexcept;

HRESULT InitializeRuntime() noexcept;

}  // namespace shim::winrt
