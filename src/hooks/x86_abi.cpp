#include "shim/x86_abi.h"

namespace shim::hooks::x86 {
namespace {

#if defined(_MSC_VER) && defined(_M_IX86)

// Pushes the stack tail right to left, loads the register pair, calls, and
// releases the tail itself.
//
// ESP is restored from EBP instead of by adding back the pushed count. The
// callee is game code reached through patched paths, and one of those splices
// (the 1.1.5 adapter register thunk) deliberately leaves EBX and EDI holding
// values from the game's frame. Nothing kept in a callee-saved register across
// the call would survive to compute the release.
__declspec(naked) void* __cdecl CallRegisterCallerCleanThunk(
    const void* /*function*/, void* /*ecx_argument*/, void* /*edx_argument*/,
    const void* const* /*stack_arguments*/,
    size_t /*stack_argument_count*/) {
  __asm {
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ebx

    mov esi, dword ptr [ebp + 14h]   // stack_arguments
    mov ecx, dword ptr [ebp + 18h]   // stack_argument_count
    test ecx, ecx
    jz load_registers

    // Right to left, so stack_arguments[0] ends up nearest the return address.
  push_argument:
    push dword ptr [esi + ecx * 4 - 4]
    dec ecx
    jnz push_argument

  load_registers:
    mov eax, dword ptr [ebp + 08h]   // function
    mov ecx, dword ptr [ebp + 0Ch]   // ecx_argument
    mov edx, dword ptr [ebp + 10h]   // edx_argument
    call eax

    lea esp, [ebp - 0Ch]             // release the tail; EAX keeps the result
    pop ebx
    pop edi
    pop esi
    pop ebp
    ret
  }
}

#endif

}  // namespace

void* CallRegisterCallerClean(const void* function, void* ecx_argument,
                              void* edx_argument,
                              const void* const* stack_arguments,
                              size_t stack_argument_count) noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
  if (function == nullptr ||
      (stack_argument_count != 0 && stack_arguments == nullptr)) {
    return nullptr;
  }
  return CallRegisterCallerCleanThunk(function, ecx_argument, edx_argument,
                                      stack_arguments, stack_argument_count);
#else
  // This project only supports the MSVC x86 ABI. The branch exists so a code
  // analyzer does not have to interpret MSVC inline assembly.
  (void)function;
  (void)ecx_argument;
  (void)edx_argument;
  (void)stack_arguments;
  (void)stack_argument_count;
  return nullptr;
#endif
}

}  // namespace shim::hooks::x86
