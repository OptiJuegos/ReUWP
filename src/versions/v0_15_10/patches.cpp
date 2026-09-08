#include "shim/version_patches.h"

#include "shim/common.h"

#include <commdlg.h>
#include <cstring>

#include "shim/app_window.h"
#include "shim/custom_skin.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/log.h"
#include "shim/object_access.h"
#include "shim/patch.h"
#include "shim/version_asm.h"
#include "shim/x86_patch.h"

namespace shim::versions::v01510 {
namespace {

// Cuatro callbacks Xbox equivalentes en Minecraft 0.15.10. La tabla
// embebida en d3dcraft usa el mismo stub HRESULT=8 / ret 4 que 1.1.5,
// pero con firmas y RVAs propios de esta version.
constexpr unsigned char k01510XboxCallbackNoOp[] = {
    0xB8, 0x08, 0x00, 0x00, 0x00, 0xC2, 0x04, 0x00};
constexpr unsigned char k01510XboxMenuSignInExpected[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0xEC, 0x28, 0x8B};
constexpr unsigned char k01510XboxSignInAExpected[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0xEC, 0x28, 0x8B};
constexpr unsigned char k01510XboxSignInBExpected[] = {
    0x83, 0xC1, 0x04, 0xE9, 0x48, 0xFB, 0xFF, 0xFF};
constexpr unsigned char k01510XboxSignInCExpected[] = {
    0x83, 0xC1, 0x08, 0xE9, 0xD8, 0xF9, 0xFF, 0xFF};

constexpr hooks::PatchSpec k01510XboxCallbackPatches[] = {
    {0x000F3520, k01510XboxMenuSignInExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxMenuSignInExpected),
     "Minecraft 0.15.10 menu Xbox sign-in no-op patch failed"},
    {0x00127A70, k01510XboxSignInAExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxSignInAExpected),
     "Minecraft 0.15.10 Xbox sign-in callback A no-op patch failed"},
    {0x001316C0, k01510XboxSignInBExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxSignInBExpected),
     "Minecraft 0.15.10 Xbox sign-in callback B no-op patch failed"},
    {0x001331C0, k01510XboxSignInCExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxSignInCExpected),
     "Minecraft 0.15.10 Xbox sign-in callback C no-op patch failed"},
};

bool InstallXboxCallbackPatches() noexcept {
  const hooks::PatchTransaction transaction(
      reinterpret_cast<uintptr_t>(game::Base()));
  const bool ok = transaction.Apply(k01510XboxCallbackPatches,
                                    CountOf(k01510XboxCallbackPatches));
  log::Write(ok ? "Minecraft 0.15.10 Xbox callbacks disabled for HWND host"
                : "Minecraft 0.15.10 Xbox callback patches incomplete");
  return ok;
}

// The executables shipped so far carry the next two patches baked in, so a
// signature mismatch there would be ambiguous: unsupported build, or an image
// that was patched ahead of time. Accepting the replacement bytes as success
// keeps a pre-patched and a stock executable on the same path.
bool InstallIdempotentPatch(const hooks::PatchSpec& patch,
                            const char* installed, const char* already,
                            const char* failed) noexcept {
  const hooks::PatchApplyResult result = hooks::ApplyIdempotentPatch(
      reinterpret_cast<uintptr_t>(game::Base()), patch);
  if (result == hooks::PatchApplyResult::kAlreadyApplied) {
    log::Write(already);
    return true;
  }
  if (result == hooks::PatchApplyResult::kApplied) {
    log::Write(installed);
    return true;
  }
  log::Write(patch.failure_message);
  log::Write(failed);
  return false;
}

// ---------------------------------------------------------------------------
// Minecraft 0.15.10 - lost-focus path
// ---------------------------------------------------------------------------
//
// sub_00788C70 is the UWP suspend pump. Its only caller is the lost-focus
// handler at 0x005FFC10 (the one that reports "LostFocus.Rift"), which parks
// the game in a GetTickCount64-bounded wait loop and drives the pump on every
// turn of it:
//
//   005FFDCF  mov   dword ptr [esi+0x3C4], 2   ; suspended
//   005FFDE0  call  <spin>                     ; loops while it returns true
//   005FFDEB  mov   ecx, [esi+0x258]
//   005FFDF1  call  0x00788C70                 ; neutralised here
//   005FFDF6  call  [KERNEL32!GetTickCount64]  ; deadline in [esi+0x3A0]
//
// The pump walks the GameControllerHandler_Windows vector at [this+0x74..0x78]
// and drives AppPlatform_Winrt at [this+0x10]; both classes come from the
// vtable RTTI at 0x00F3B258 and 0x00F6179C. Neither is reachable outside the
// app container: the WinRT gamepad stack is absent (the host ships an
// XInputGetState stub) and AppPlatform_Winrt is what the shim replaces.
//
// Under UWP this path runs only on a real suspend. On a Win32 window it runs on
// every alt-tab, so it has to become a no-op. The wait loop around it is left
// alone.
//
// The function takes no stack arguments - it ends in pop edi/esi/ebx, mov esp,
// ebp, pop ebp, ret - so returning immediately keeps the stack balanced. Only
// the first byte changes; the rest of the signature is there because a lone
// 0x55 matches every prologue in the image.
constexpr unsigned char k01510SuspendPumpExpected[9] = {
    0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x8B, 0xD9};
constexpr unsigned char k01510SuspendPumpReplacement[9] = {
    0xC3, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x8B, 0xD9};

constexpr hooks::PatchSpec k01510SuspendPumpPatch = {
    0x00388C70, k01510SuspendPumpExpected, k01510SuspendPumpReplacement,
    sizeof(k01510SuspendPumpExpected),
    "Minecraft 0.15.10 UWP suspend pump no-op patch failed"};

// Null guard in the teardown callback at 0x00438900, reached through the
// _Do_call slot of a std::function wrapping a lambda:
//
//   001DC7A0  mov ecx, [ecx+4]      ; captured object
//   001DC7A3  mov ecx, [ecx+0x34]
//   001DC7A6  jmp 0x00438900
//
// The original guards the outer pointer and only then dereferences it:
//
//   mov ecx,[esi+0xA8] / test ecx,ecx / je +8 / mov ecx,[ecx+0x20] / call
//
// Under the HWND host the outer object is always constructed but its 0x20
// member can be null, so the call landed with a null `this`. Moving the test
// one level in fixes that; the branch target is unchanged, so both forms skip
// the same call.
//
// This DROPS the test on [esi+0xA8] itself - seven bytes cannot hold both - so
// a null outer pointer now faults where it used to be tolerated. That is the
// trade the shipped executables already make, and it is the reason this patch
// gets its own table: if [esi+0xA8] ever comes back null, this is the entry to
// revisit.
constexpr unsigned char k01510TeardownGuardExpected[13] = {
    0x8B, 0x8E, 0xA8, 0x00, 0x00, 0x00,
    0x85, 0xC9, 0x74, 0x08, 0x8B, 0x49, 0x20};
constexpr unsigned char k01510TeardownGuardReplacement[13] = {
    0x8B, 0x8E, 0xA8, 0x00, 0x00, 0x00,
    0x8B, 0x49, 0x20, 0x85, 0xC9, 0x74, 0x05};

constexpr hooks::PatchSpec k01510TeardownGuardPatch = {
    0x00038D01, k01510TeardownGuardExpected, k01510TeardownGuardReplacement,
    sizeof(k01510TeardownGuardExpected),
    "Minecraft 0.15.10 teardown null-guard patch failed"};

bool InstallSuspendPumpPatch() noexcept {
  return InstallIdempotentPatch(
      k01510SuspendPumpPatch,
      "Minecraft 0.15.10 UWP suspend pump disabled",
      "Minecraft 0.15.10 UWP suspend pump already disabled",
      "Minecraft 0.15.10 UWP suspend pump patch unavailable");
}

bool InstallTeardownGuardPatch() noexcept {
  return InstallIdempotentPatch(
      k01510TeardownGuardPatch,
      "Minecraft 0.15.10 teardown null guard installed",
      "Minecraft 0.15.10 teardown null guard already installed",
      "Minecraft 0.15.10 teardown null guard unavailable");
}

// ---------------------------------------------------------------------------
// Minecraft 0.15.10 - picker de skin + fullscreen persistentes
// ---------------------------------------------------------------------------

constexpr DWORD k01510CustomSkinRva = 0x00600110;
constexpr DWORD k01510FullscreenRva = 0x00601310;

constexpr unsigned char k01510CustomSkinSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xFB, 0x54, 0xD0, 0x00};
constexpr unsigned char k01510FullscreenSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x3F, 0x59, 0xD0, 0x00};

// sub_62950980. Igual que el 0.13.2 picker, el callback se conserva en
// this+0x1B0 y se notifica por vtable+8 al terminar el dialogo Win32.
void __fastcall CustomSkinHook01510(void* self, void* /*edx*/,
                                    void* callback) noexcept {
  if (self != nullptr &&
      !::IsBadWritePtr(object_access::Field(self, 0x1B0), sizeof(void*))) {
    *reinterpret_cast<void**>(object_access::Field(self, 0x1B0)) = callback;
  }

  custom_skin::ShowAndInstall();

  void* const finish_entry = object_access::VtableEntry(callback, 8);
  if (finish_entry != nullptr) {
    using FinishFn = void(__thiscall*)(void* callback);
    reinterpret_cast<FinishFn>(finish_entry)(callback);
  }
}

// sub_629512B0 es __stdcall(int): el valor ya llega como argumento, no dentro
// de un objeto C++/CX como ocurre en el wrapper de 1.1.5.
void SHIM_COM FullscreenHook01510(int enabled) noexcept {
  app_window::SetBorderlessFullscreen(enabled != 0);
}

bool Install01510UiHooks() noexcept {
  if (game::Current() != game::Version::kV0_15_10) {
    return true;
  }

  const hooks::x86::RelativeBranchPatch patches[] = {
      {k01510CustomSkinRva, k01510CustomSkinSignature,
       sizeof(k01510CustomSkinSignature),
       reinterpret_cast<const void*>(&CustomSkinHook01510),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 custom-skin hook failed"},
      {k01510FullscreenRva, k01510FullscreenSignature,
       sizeof(k01510FullscreenSignature),
       reinterpret_cast<const void*>(&FullscreenHook01510),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 fullscreen hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "0.15.10 custom-skin/fullscreen hooks installed"
                : "0.15.10 custom-skin/fullscreen hooks unavailable");
  return ok;
}

// ---------------------------------------------------------------------------
// Minecraft 0.15.10 - Import/Export World directo a Win32
// ---------------------------------------------------------------------------
//
// sub_62953950 sustituye game+0x43CE0, una funcion thiscall enorme con 54
// DWORDs de argumentos. Declarar esa firma en C++ seria fragil e innecesario:
// el original conserva los primeros diez bytes en un trampoline y solo consume
// los argumentos cuando el contexto final es FileBrowser.Rift.Import/Export.
// El bridge naked de abajo hace lo mismo, dejando la pila intacta para el
// camino original.

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

bool Install01510FileBrowserHook() noexcept {
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

// ---------------------------------------------------------------------------
// Minecraft 0.15.10 - material scheduler sincrono
// ---------------------------------------------------------------------------
//
// El juego despacha la compilacion de materiales a un scheduler que dependia
// del entorno UWP. d3dcraft intercepta un CALL concreto (game+0x2CCA0B),
// reconoce el tipo de tarea por su vtable y ejecuta su metodo +8 hasta que
// indique terminado. Despues hace tail-jump al scheduler original con ECX y la
// pila intactos. El bridge naked conserva precisamente ese contrato thiscall.

void* g_original_material_scheduler_01510 = nullptr;
volatile LONG g_material_shader_sync_count_01510 = 0;

void __cdecl DrainMaterialShaderTask01510(void* callback) noexcept {
  if (callback == nullptr || ::IsBadReadPtr(callback, sizeof(void*))) {
    return;
  }

  void* const vtable = *reinterpret_cast<void**>(callback);
  void* const expected_vtable = game::Resolve(0x00B3A334);
  if (vtable != expected_vtable || vtable == nullptr ||
      ::IsBadReadPtr(vtable, 3 * sizeof(void*))) {
    return;
  }

  void* const step_entry = reinterpret_cast<void**>(vtable)[2];
  if (step_entry == nullptr) {
    return;
  }

  using StepFn = unsigned char(__thiscall*)(void* callback);
  const auto step = reinterpret_cast<StepFn>(step_entry);
  while (step(callback) == 0) {
  }

  const LONG count = ::InterlockedIncrement(&g_material_shader_sync_count_01510);
  if (count <= 4) {
    log::Write("material shader task executed synchronously");
  }
}


bool Install01510MaterialSchedulerPatch() noexcept {
  if (game::Current() != game::Version::kV0_15_10) {
    return true;
  }

  constexpr DWORD kCallsiteRva = 0x002CCA0B;
  constexpr DWORD kOriginalSchedulerRva = 0x00390670;
  constexpr unsigned char kExpected[5] = {
      0xE8, 0x60, 0x3C, 0x0C, 0x00};

  auto* const target = static_cast<unsigned char*>(game::Resolve(kCallsiteRva));
  g_original_material_scheduler_01510 = game::Resolve(kOriginalSchedulerRva);
  if (target == nullptr || g_original_material_scheduler_01510 == nullptr ||
      !hooks::MatchesSignature(target, kExpected, sizeof(kExpected))) {
    log::Write("0.15.10 material scheduler hook signature mismatch");
    g_original_material_scheduler_01510 = nullptr;
    return false;
  }

  const void* const entry = asm_hooks::MaterialSchedulerEntry();
  if (entry == nullptr) {
    log::Write("0.15.10 material scheduler hook requires MSVC x86 assembly support");
    g_original_material_scheduler_01510 = nullptr;
    return false;
  }
  asm_hooks::ConfigureMaterialScheduler(&DrainMaterialShaderTask01510,
                                        g_original_material_scheduler_01510);
  const hooks::x86::RelativeBranchPatch patch = {
      kCallsiteRva, kExpected, sizeof(kExpected), entry,
      hooks::x86::RelativeBranch::kCall,
      "0.15.10 material scheduler compatibility patch failed"};
  if (!hooks::x86::ApplyRelativeBranchPatches(
          reinterpret_cast<uintptr_t>(game::Base()), &patch, 1)) {
    asm_hooks::ConfigureMaterialScheduler(nullptr, nullptr);
    g_original_material_scheduler_01510 = nullptr;
    return false;
  }

  log::Write("0.15.10 material scheduler compatibility patch installed");
  return true;
}


}  // namespace

bool InstallCompatibilityPatches() noexcept {
  // Every result is collected before the AND so that one failing table cannot
  // short-circuit the others: these three are independent, and a build whose
  // signatures moved for one of them should still get the other two.
  const bool xbox = InstallXboxCallbackPatches();
  const bool suspend_pump = InstallSuspendPumpPatch();
  const bool teardown_guard = InstallTeardownGuardPatch();
  return xbox && suspend_pump && teardown_guard;
}

void InstallPostWindowPatches() noexcept {
  input::InstallHooks();
  Install01510UiHooks();
  Install01510FileBrowserHook();
  Install01510MaterialSchedulerPatch();
}

}  // namespace shim::versions::v01510
