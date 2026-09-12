#include "shim/version_asm.h"

namespace shim::versions::v01510::asm_hooks {
namespace {

void** g_chat_active_screen_slot = nullptr;
bool* g_chat_text_input_active = nullptr;
void* g_chat_original_open = nullptr;

FileBrowserHandler g_file_browser_handler = nullptr;
void* g_file_browser_trampoline = nullptr;

MaterialTaskHandler g_material_task_handler = nullptr;
void* g_material_scheduler_original = nullptr;

#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void ChatScreenOpenHook() {
  __asm {
    mov eax, dword ptr [g_chat_active_screen_slot]
    test eax, eax
    jz skip_active_screen
    mov dword ptr [eax], ecx
  skip_active_screen:
    mov eax, dword ptr [g_chat_text_input_active]
    test eax, eax
    jz skip_text_state
    mov byte ptr [eax], 1
  skip_text_state:
    mov eax, dword ptr [g_chat_original_open]
    test eax, eax
    jz no_original
    jmp eax
  no_original:
    ret
  }
}

__declspec(naked) void FileBrowserHook() {
  __asm {
    push ebx
    push esi
    mov esi, ecx
    lea eax, [esp + 0Ch]
    push eax
    push esi
    call dword ptr [g_file_browser_handler]
    add esp, 8
    test al, al
    jne handled

    mov ecx, esi
    pop esi
    pop ebx
    jmp dword ptr [g_file_browser_trampoline]

  handled:
    pop esi
    pop ebx
    ret 0D8h
  }
}

__declspec(naked) void MaterialSchedulerHook() {
  __asm {
    push ebx
    push edi
    push esi
    mov esi, ecx
    mov edi, dword ptr [esp + 14h]
    push edi
    call dword ptr [g_material_task_handler]
    add esp, 4
    mov ecx, esi
    pop esi
    pop edi
    pop ebx
    jmp dword ptr [g_material_scheduler_original]
  }
}
#else
void ChatScreenOpenHook() noexcept {}
void FileBrowserHook() noexcept {}
void MaterialSchedulerHook() noexcept {}
#endif

}  // namespace

const void* ChatScreenOpenEntry() noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
  return reinterpret_cast<const void*>(&ChatScreenOpenHook);
#else
  return nullptr;
#endif
}

void ConfigureChatScreenOpen(void** active_screen_slot, bool* text_input_active,
                             void* original) noexcept {
  g_chat_active_screen_slot = active_screen_slot;
  g_chat_text_input_active = text_input_active;
  g_chat_original_open = original;
}

const void* FileBrowserEntry() noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
  return reinterpret_cast<const void*>(&FileBrowserHook);
#else
  return nullptr;
#endif
}

void ConfigureFileBrowser(FileBrowserHandler handler,
                          void* trampoline) noexcept {
  g_file_browser_handler = handler;
  g_file_browser_trampoline = trampoline;
}

const void* MaterialSchedulerEntry() noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
  return reinterpret_cast<const void*>(&MaterialSchedulerHook);
#else
  return nullptr;
#endif
}

void ConfigureMaterialScheduler(MaterialTaskHandler handler,
                                void* original) noexcept {
  g_material_task_handler = handler;
  g_material_scheduler_original = original;
}

}  // namespace shim::versions::v01510::asm_hooks
