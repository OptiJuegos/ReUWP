#include "shim/version_asm.h"

namespace shim::versions::v0132::asm_hooks {
namespace {

DWORD g_main_thread_id = 0;
void* g_original_frame_sleep = nullptr;

#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void __cdecl FrameSleepHook() {
  __asm {
    call GetCurrentThreadId
    cmp eax, dword ptr [g_main_thread_id]
    jne forward_original

    push 0
    call Sleep
    ret

  forward_original:
    cmp dword ptr [g_original_frame_sleep], 0
    je no_original
    jmp dword ptr [g_original_frame_sleep]

  no_original:
    ret
  }
}
#else
void __cdecl FrameSleepHook() noexcept {}
#endif

}  // namespace

void* CallTextFactory(void* factory, const char* utf8) noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
  void* event = nullptr;
  void** out_event = &event;
  unsigned char sequence = 0;
  unsigned char* sequence_ptr = &sequence;
  __asm {
    mov eax, factory
    mov ecx, out_event
    mov edx, utf8
    push sequence_ptr
    call eax
    add esp, 4
  }
  return event;
#else
  (void)factory;
  (void)utf8;
  return nullptr;
#endif
}

const void* FrameSleepEntry() noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
  return reinterpret_cast<const void*>(&FrameSleepHook);
#else
  return nullptr;
#endif
}

void ConfigureFrameSleep(DWORD main_thread_id, void* original) noexcept {
  g_main_thread_id = main_thread_id;
  g_original_frame_sleep = original;
}

}  // namespace shim::versions::v0132::asm_hooks
