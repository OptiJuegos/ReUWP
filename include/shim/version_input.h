#pragma once

namespace shim::versions::v0132::input_backend {
void SetAppMain(void* app_main) noexcept;
void* Handler() noexcept;
bool InstallHooks() noexcept;
bool InstallLifecycleHooks() noexcept;
bool RequestSubmit() noexcept;
void PumpSubmit() noexcept;
bool PostPointer(int x, int y, bool relative, unsigned char action,
                 unsigned char pressed) noexcept;
bool PostKey(unsigned int virtual_key, bool down) noexcept;
bool PostChar(unsigned int codepoint) noexcept;
bool ReadCursorState(bool* relative, int* command) noexcept;
void WriteCursorMode(bool relative) noexcept;
}  // namespace shim::versions::v0132::input_backend

namespace shim::versions::v01510::input_backend {
void SetAppMain(void* app_main) noexcept;
void* Handler() noexcept;
bool InstallHooks() noexcept;
bool InstallLifecycleHooks() noexcept;
bool RequestSubmit() noexcept;
void PumpSubmit() noexcept;
bool PostPointer(int x, int y, bool relative, unsigned char action,
                 unsigned char pressed) noexcept;
bool PostKey(unsigned int virtual_key, bool down) noexcept;
bool PostChar(unsigned int codepoint) noexcept;
bool ReadCursorState(bool* relative, int* command) noexcept;
void WriteCursorMode(bool relative) noexcept;
}  // namespace shim::versions::v01510::input_backend

namespace shim::versions::v115::input_backend {
void SetAppMain(void* app_main) noexcept;
void* Handler() noexcept;
bool InstallHooks() noexcept;
bool InstallLifecycleHooks() noexcept;
bool RequestSubmit() noexcept;
void PumpSubmit() noexcept;
bool PostPointer(int x, int y, bool relative, unsigned char action,
                 unsigned char pressed) noexcept;
bool PostKey(unsigned int virtual_key, bool down) noexcept;
bool PostChar(unsigned int codepoint) noexcept;
bool ReadCursorState(bool* relative, int* command) noexcept;
void WriteCursorMode(bool relative) noexcept;
}  // namespace shim::versions::v115::input_backend
