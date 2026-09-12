#include "shim/version_asm.h"

namespace shim::versions::v115::asm_hooks {
namespace {

volatile DWORD g_adapter_function = 0;
volatile DWORD g_adapter_self = 0;
volatile DWORD g_adapter_arg0 = 0;
volatile DWORD g_adapter_arg1 = 0;
volatile DWORD g_adapter_arg2 = 0;

#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void __cdecl AdapterRegisterThunk() {
  __asm {
    pop eax
    add esp, 8
    mov edi, dword ptr [ebp - 10h]
    mov ebx, dword ptr [edi + 10h]
    mov ecx, dword ptr [edi]

    mov edx, dword ptr [ecx + 4]
    mov dword ptr [g_adapter_function], edx
    mov dword ptr [g_adapter_self], edi
    mov dword ptr [g_adapter_arg0], ebx
    mov dword ptr [g_adapter_arg1], ecx
    mov dword ptr [g_adapter_arg2], eax

    push eax
    push edi
    call dword ptr [ecx + 4]
    ret
  }
}
#else
void __cdecl AdapterRegisterThunk() noexcept {}
#endif

}  // namespace

const void* AdapterRegisterEntry() noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
  return reinterpret_cast<const void*>(&AdapterRegisterThunk);
#else
  return nullptr;
#endif
}

AdapterSnapshot GetAdapterSnapshot() noexcept {
  AdapterSnapshot snapshot = {};
  snapshot.function = g_adapter_function;
  snapshot.self = g_adapter_self;
  snapshot.arg0 = g_adapter_arg0;
  snapshot.arg1 = g_adapter_arg1;
  snapshot.arg2 = g_adapter_arg2;
  return snapshot;
}

}  // namespace shim::versions::v115::asm_hooks
