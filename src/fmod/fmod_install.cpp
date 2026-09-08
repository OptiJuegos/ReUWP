#include "shim/fmod_install.h"

#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"

namespace shim::fmod {
namespace {

struct DelayDescriptor {
  DWORD attributes;
  DWORD name_rva;
  DWORD module_rva;
  DWORD iat_rva;
  DWORD int_rva;
  DWORD bound_iat_rva;
  DWORD unload_iat_rva;
  DWORD timestamp;
};

constexpr wchar_t kDesktopFmodName[] = L"fmod.dll";
constexpr char kExpectedImportName[] = "fmod.dll";
struct InstallState {
  const game::FmodLayout* layout = nullptr;
};

InstallState g_state = {};

DWORD CountImports(const DWORD* names, DWORD limit) noexcept {
  DWORD count = 0;
  while (count < limit && names[count] != 0) {
    ++count;
  }
  return count;
}

const char* ImportName(DWORD entry, unsigned char* base) noexcept {
  if ((entry & 0x80000000u) != 0) {
    return reinterpret_cast<const char*>(entry & 0xFFFFu);
  }
  return reinterpret_cast<const char*>(base + entry + 2);
}

}  // namespace

void BindLayout(const game::FmodLayout* layout) noexcept {
  g_state.layout = layout;
}

const game::FmodLayout* BoundLayout() noexcept {
  return g_state.layout;
}

bool Install() noexcept {
  const game::VersionProfile* const profile = game::CurrentProfile();
  if (profile == nullptr || profile->fmod == nullptr ||
      profile->fmod->install == nullptr) {
    return false;
  }
  return profile->fmod->install();
}

bool InstallBridgeArray(DWORD name_rva, DWORD iat_rva, DWORD expected_count,
                        const void* const* bridge_slots, size_t bridge_count,
                        const char* label) noexcept {
  const char* name = static_cast<const char*>(game::Resolve(name_rva));
  if (name == nullptr || ::lstrcmpA(name, kExpectedImportName) != 0) {
    log::Writef("%s FMOD mismatch: import name is not fmod.dll", label);
    return false;
  }
  if (expected_count != bridge_count) {
    log::Writef("%s FMOD slot count mismatch: layout=%u bridge=%u", label,
                expected_count, static_cast<unsigned int>(bridge_count));
    return false;
  }

  auto** slots = static_cast<void**>(game::Resolve(iat_rva));
  if (slots == nullptr) {
    return false;
  }

  const SIZE_T span = bridge_count * sizeof(void*);
  DWORD previous_protection = 0;
  if (!::VirtualProtect(slots, span, PAGE_READWRITE, &previous_protection)) {
    log::Writef("%s FMOD delay-IAT protection change failed", label);
    return false;
  }

  for (size_t index = 0; index < bridge_count; ++index) {
    slots[index] = const_cast<void*>(bridge_slots[index]);
  }

  DWORD ignored = 0;
  ::VirtualProtect(slots, span, previous_protection, &ignored);
  ::FlushInstructionCache(::GetCurrentProcess(), slots, span);
  log::Writef("%s: %u FMOD delay-IAT slots replaced with Win32 bridge", label,
              static_cast<unsigned int>(bridge_count));
  return true;
}

bool ResolveDesktopDelayImports(DWORD descriptor_rva, DWORD expected_iat_rva,
                                DWORD expected_int_rva,
                                DWORD expected_import_count) noexcept {
  auto* base = static_cast<unsigned char*>(static_cast<void*>(game::Base()));
  const auto* descriptor = static_cast<const DelayDescriptor*>(
      game::Resolve(descriptor_rva));
  if (descriptor == nullptr) {
    return false;
  }

  if (descriptor->attributes != 1 ||
      descriptor->iat_rva != expected_iat_rva ||
      descriptor->int_rva != expected_int_rva) {
    log::Write("FMOD delay descriptor mismatch");
    return false;
  }

  const char* import_name =
      reinterpret_cast<const char*>(base + descriptor->name_rva);
  if (::lstrcmpA(import_name, kExpectedImportName) != 0) {
    log::Write("FMOD delay descriptor mismatch");
    return false;
  }

  const auto* names = reinterpret_cast<const DWORD*>(base + descriptor->int_rva);
  auto** slots = reinterpret_cast<void**>(base + descriptor->iat_rva);
  const DWORD found = CountImports(names, expected_import_count + 8);
  if (found != expected_import_count) {
    log::Writef("FMOD delay import count mismatch: %u, expected %u", found,
                expected_import_count);
    return false;
  }

  const HMODULE module = ::LoadLibraryW(kDesktopFmodName);
  if (module == nullptr) {
    log::Write("desktop FMOD load failed");
    return false;
  }

  const SIZE_T span = found * sizeof(void*);
  DWORD previous_protection = 0;
  if (!::VirtualProtect(slots, span, PAGE_READWRITE, &previous_protection)) {
    log::Write("FMOD delay-IAT protection change failed");
    return false;
  }

  bool complete = true;
  for (DWORD index = 0; index < found; ++index) {
    const char* name = ImportName(names[index], base);
    FARPROC address = ::GetProcAddress(module, name);
    if (address == nullptr) {
      log::Writef("desktop FMOD export missing: %s",
                  (names[index] & 0x80000000u) != 0 ? "(ordinal)" : name);
      complete = false;
      break;
    }
    slots[index] = reinterpret_cast<void*>(address);
  }

  DWORD ignored = 0;
  ::VirtualProtect(slots, span, previous_protection, &ignored);
  ::FlushInstructionCache(::GetCurrentProcess(), slots, span);

  if (complete) {
    log::Writef("desktop FMOD delay imports resolved (%u)", found);
  }
  return complete;
}

}  // namespace shim::fmod
