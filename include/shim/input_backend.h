#pragma once

#include "shim/game_layout.h"
#include "shim/memory.h"

namespace shim::input::backend {

inline unsigned char* Field(void* object, DWORD offset) noexcept {
  return reinterpret_cast<unsigned char*>(object) + offset;
}

inline void* ReadPointer(const void* address) noexcept {
  if (!memory::IsReadable(address, sizeof(void*))) {
    return nullptr;
  }
  return *reinterpret_cast<void* const*>(address);
}

template <typename Fn>
Fn GameFunction(DWORD rva) noexcept {
  void* const entry = game::Resolve(rva);
  return memory::IsExecutable(entry) ? reinterpret_cast<Fn>(entry) : nullptr;
}

int EncodeUtf8(unsigned int codepoint, char (&utf8)[5]) noexcept;
void ActivateTextInput() noexcept;
void DeactivateTextInput() noexcept;
bool* TextInputActiveFlag() noexcept;

}  // namespace shim::input::backend
