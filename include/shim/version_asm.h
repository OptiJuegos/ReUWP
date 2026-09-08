#pragma once

#include "shim/common.h"

namespace shim::versions::v0132::asm_hooks {

void* CallTextFactory(void* factory, const char* utf8) noexcept;
const void* FrameSleepEntry() noexcept;
void ConfigureFrameSleep(DWORD main_thread_id, void* original) noexcept;

}  // namespace shim::versions::v0132::asm_hooks

namespace shim::versions::v01510::asm_hooks {

const void* ChatScreenOpenEntry() noexcept;
void ConfigureChatScreenOpen(void** active_screen_slot, bool* text_input_active,
                             void* original) noexcept;

using FileBrowserHandler = int(__cdecl*)(void*, DWORD*);
const void* FileBrowserEntry() noexcept;
void ConfigureFileBrowser(FileBrowserHandler handler,
                          void* trampoline) noexcept;

using MaterialTaskHandler = void(__cdecl*)(void*);
const void* MaterialSchedulerEntry() noexcept;
void ConfigureMaterialScheduler(MaterialTaskHandler handler,
                                void* original) noexcept;

}  // namespace shim::versions::v01510::asm_hooks

namespace shim::versions::v115::asm_hooks {

struct AdapterSnapshot {
  DWORD function;
  DWORD self;
  DWORD arg0;
  DWORD arg1;
  DWORD arg2;
};

const void* AdapterRegisterEntry() noexcept;
AdapterSnapshot GetAdapterSnapshot() noexcept;

}  // namespace shim::versions::v115::asm_hooks
