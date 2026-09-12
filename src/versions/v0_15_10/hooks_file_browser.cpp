#include "patches_internal.h"

#include "shim/common.h"

#include <commdlg.h>
#include <cstring>

#include "shim/app_window.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/log.h"
#include "shim/patch.h"
#include "shim/version_asm.h"
#include "shim/x86_patch.h"

namespace shim::versions::v01510::detail {
namespace {

constexpr DWORD k01510FileBrowserRva = 0x00043CE0;
constexpr unsigned char k01510FileBrowserSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x41, 0x48, 0xC9, 0x00};

void* g_file_browser_trampoline_01510 = nullptr;

struct GameString24_01510 {
  union {
    char inline_buffer[16];
    char* heap;
  } storage;
  DWORD size;
  DWORD capacity;
};
static_assert(sizeof(GameString24_01510) == 24,
              "0.15.10 std::string ABI must stay 24 bytes");
static_assert(offsetof(GameString24_01510, size) == 0x10,
              "0.15.10 std::string size must stay at +0x10");
static_assert(offsetof(GameString24_01510, capacity) == 0x14,
              "0.15.10 std::string capacity must stay at +0x14");

constexpr char k01510FileBrowserImport[] = "FileBrowser.Rift.Import";
constexpr char k01510FileBrowserExport[] = "FileBrowser.Rift.Export";
constexpr wchar_t k01510WorldFilter[] =
    L"Minecraft World (*.mcworld)\0*.mcworld\0All files (*.*)\0*.*\0\0";
constexpr wchar_t k01510WorldExtension[] = L"mcworld";
constexpr wchar_t k01510ImportTitle[] = L"Import Minecraft World";
constexpr wchar_t k01510ExportTitle[] = L"Export Minecraft World";
constexpr wchar_t k01510ExportName[] = L"world.mcworld";

// Los seis DWORDs finales de la funcion forman un std::string MSVC x86. La
// cadena usa SSO hasta capacity=15; con capacity>=16 el primer DWORD es un
// char* de heap. El DLL exige size==23 antes de comparar el contexto.
const char* FileBrowserContext01510(const DWORD* args) noexcept {
  if (args == nullptr || ::IsBadReadPtr(args, 54 * sizeof(DWORD))) {
    return nullptr;
  }

  const DWORD size = args[52];
  const DWORD capacity = args[53];
  if (size != 23) {
    return nullptr;
  }

  if (capacity < 16) {
    return reinterpret_cast<const char*>(args + 48);
  }

  const char* const text = reinterpret_cast<const char*>(
      static_cast<uintptr_t>(args[48]));
  if (text == nullptr || ::IsBadReadPtr(text, size)) {
    return nullptr;
  }
  return text;
}

bool ContextEquals01510(const char* context, const char* expected) noexcept {
  return context != nullptr && expected != nullptr &&
         !::IsBadReadPtr(context, 23) && memcmp(context, expected, 23) == 0;
}

bool BuildGameString01510(const wchar_t* path, GameString24_01510* out,
                          char** heap_payload) noexcept {
  if (path == nullptr || out == nullptr || heap_payload == nullptr) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  *heap_payload = nullptr;

  const int required = ::WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0,
                                              nullptr, nullptr);
  if (required < 2) {
    return false;
  }

  const DWORD size = static_cast<DWORD>(required - 1);
  out->size = size;

  if (required <= 16) {
    out->capacity = 15;
    return ::WideCharToMultiByte(CP_UTF8, 0, path, -1,
                                 out->storage.inline_buffer, 16, nullptr,
                                 nullptr) != 0;
  }

  char* const payload = static_cast<char*>(
      ::HeapAlloc(::GetProcessHeap(), 0, static_cast<SIZE_T>(required)));
  if (payload == nullptr) {
    return false;
  }

  if (::WideCharToMultiByte(CP_UTF8, 0, path, -1, payload, required, nullptr,
                            nullptr) == 0) {
    ::HeapFree(::GetProcessHeap(), 0, payload);
    return false;
  }

  out->storage.heap = payload;
  out->capacity = size;
  *heap_payload = payload;
  return true;
}

void InvokeFileBrowserCallback01510(void* callback,
                                    GameString24_01510* path,
                                    DWORD* args) noexcept {
  if (callback == nullptr || path == nullptr || args == nullptr ||
      ::IsBadReadPtr(callback, sizeof(void*))) {
    return;
  }

  void** const vtable = *reinterpret_cast<void***>(callback);
  if (vtable == nullptr || ::IsBadReadPtr(vtable, 3 * sizeof(void*))) {
    return;
  }

  void* const entry = vtable[2];  // vtable + 8, igual que 62953C20..62953C40.
  if (entry == nullptr) {
    return;
  }

  using CallbackFn = void(__thiscall*)(void*, GameString24_01510*, DWORD*);
  reinterpret_cast<CallbackFn>(entry)(callback, path, args);
}

// El wrapper original destruye primero el agregado de argumentos con
// game+0x43AD0 y, si el std::string de contexto era heap-backed, libera luego
// su payload con game+0x245A0(ptr, capacity+1, 1).
void CleanupFileBrowserArgs01510(DWORD* args) noexcept {
  if (args == nullptr || ::IsBadReadPtr(args, 54 * sizeof(DWORD))) {
    return;
  }

  const uintptr_t base = reinterpret_cast<uintptr_t>(game::Base());
  if (base == 0) {
    return;
  }

  using DestroyArgsFn = void(__thiscall*)(void*);
  reinterpret_cast<DestroyArgsFn>(base + 0x00043AD0)(args);

  const DWORD capacity = args[53];
  if (capacity < 16) {
    return;
  }

  void* const payload = reinterpret_cast<void*>(
      static_cast<uintptr_t>(args[48]));
  if (payload == nullptr || ::IsBadReadPtr(payload, 1)) {
    return;
  }

  using FreeStringFn = void(__cdecl*)(void*, DWORD, int);
  reinterpret_cast<FreeStringFn>(base + 0x000245A0)(
      payload, capacity + 1, 1);
}

// Devuelve 1 solo si el contexto pertenece al FileBrowser Rift. En ese caso el
// bridge consume toda la llamada con `ret 0xD8`; cualquier otro contexto cae
// al trampoline y ejecuta la funcion original sin haber tocado sus argumentos.
int __cdecl HandleFileBrowser01510(void* /*self*/, DWORD* args) noexcept {
  const char* const context = FileBrowserContext01510(args);
  const bool is_import = ContextEquals01510(context, k01510FileBrowserImport);
  const bool is_export = ContextEquals01510(context, k01510FileBrowserExport);
  if (!is_import && !is_export) {
    return 0;
  }

  wchar_t path[MAX_PATH] = {};
  if (is_export) {
    ::lstrcpynW(path, k01510ExportName, CountOf(path));
  }

  OPENFILENAMEW ofn = {};
  // El original codifica 0x58 explicitamente (OPENFILENAMEW x86).
  ofn.lStructSize = 0x58;
  ofn.hwndOwner = app_window::Handle();
  ofn.lpstrFilter = k01510WorldFilter;
  ofn.lpstrFile = path;
  ofn.nMaxFile = CountOf(path);
  ofn.lpstrTitle = is_export ? k01510ExportTitle : k01510ImportTitle;
  ofn.Flags = is_export ? 0x0008080E : 0x0008180C;
  ofn.lpstrDefExt = k01510WorldExtension;

  input::RequestCursorMode(false);
  input::InvalidateCursorClip();
  input::SyncCursorMode(app_window::Handle());

  const BOOL picked = is_export ? ::GetSaveFileNameW(&ofn)
                                : ::GetOpenFileNameW(&ofn);

  if (picked != FALSE) {
    GameString24_01510 selected = {};
    char* local_heap = nullptr;
    if (BuildGameString01510(path, &selected, &local_heap)) {
      void* const callback = reinterpret_cast<void*>(
          static_cast<uintptr_t>(args[29]));
      InvokeFileBrowserCallback01510(callback, &selected, args);
    }
    if (local_heap != nullptr) {
      ::HeapFree(::GetProcessHeap(), 0, local_heap);
    }
  }

  CleanupFileBrowserArgs01510(args);
  return 1;
}


bool CreateFileBrowserTrampoline01510(void* target) noexcept {
  if (target == nullptr) {
    return false;
  }

  auto* const trampoline = static_cast<unsigned char*>(
      ::VirtualAlloc(nullptr, 0x20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (trampoline == nullptr) {
    return false;
  }

  memcpy(trampoline, target, 10);
  trampoline[10] = 0xB8;
  *reinterpret_cast<DWORD*>(trampoline + 11) =
      reinterpret_cast<DWORD>(static_cast<unsigned char*>(target) + 10);
  trampoline[15] = 0xFF;
  trampoline[16] = 0xE0;

  DWORD old_protect = 0;
  if (!::VirtualProtect(trampoline, 0x20, PAGE_EXECUTE_READ, &old_protect)) {
    ::VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  ::FlushInstructionCache(::GetCurrentProcess(), trampoline, 0x20);
  g_file_browser_trampoline_01510 = trampoline;
  return true;
}

}  // namespace

bool InstallFileBrowserHook() noexcept {
  if (game::Current() != game::Version::kV0_15_10) {
    return true;
  }

  const void* const entry = asm_hooks::FileBrowserEntry();
  if (entry == nullptr) {
    log::Write("0.15.10 FileBrowser hook requires MSVC x86 assembly support");
    return false;
  }

  auto* const target =
      static_cast<unsigned char*>(game::Resolve(k01510FileBrowserRva));
  if (target == nullptr ||
      !hooks::MatchesSignature(target, k01510FileBrowserSignature,
                               sizeof(k01510FileBrowserSignature))) {
    log::Write("0.15.10 FileBrowser hook signature mismatch");
    return false;
  }

  if (!CreateFileBrowserTrampoline01510(target)) {
    log::Write("0.15.10 FileBrowser trampoline creation failed");
    return false;
  }

  asm_hooks::ConfigureFileBrowser(&HandleFileBrowser01510,
                                  g_file_browser_trampoline_01510);
  const hooks::x86::RelativeBranchPatch patch = {
      k01510FileBrowserRva, k01510FileBrowserSignature,
      sizeof(k01510FileBrowserSignature), entry,
      hooks::x86::RelativeBranch::kJump,
      "0.15.10 FileBrowser hook write failed"};
  if (!hooks::x86::ApplyRelativeBranchPatches(
          reinterpret_cast<uintptr_t>(game::Base()), &patch, 1)) {
    ::VirtualFree(g_file_browser_trampoline_01510, 0, MEM_RELEASE);
    g_file_browser_trampoline_01510 = nullptr;
    asm_hooks::ConfigureFileBrowser(nullptr, nullptr);
    return false;
  }

  log::Write("0.15.10 direct Import/Export World FileBrowser installed");
  return true;
}


}  // namespace shim::versions::v01510::detail
