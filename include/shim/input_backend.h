#pragma once

#include "shim/game_layout.h"

namespace shim::input::backend {

inline unsigned char* Field(void* object, DWORD offset) noexcept {
  return reinterpret_cast<unsigned char*>(object) + offset;
}

inline void* ReadPointer(const void* address) noexcept {
  if (address == nullptr || ::IsBadReadPtr(address, sizeof(void*))) {
    return nullptr;
  }
  return *reinterpret_cast<void* const*>(address);
}

template <typename Fn>
Fn GameFunction(DWORD rva) noexcept {
  return reinterpret_cast<Fn>(game::Resolve(rva));
}

int EncodeUtf8(unsigned int codepoint, char (&utf8)[5]) noexcept;
void ActivateTextInput() noexcept;
void DeactivateTextInput() noexcept;
bool* TextInputActiveFlag() noexcept;

}  // namespace shim::input::backend
