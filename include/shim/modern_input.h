#pragma once

namespace shim::game {
struct InputLayout;
}

namespace shim::versions::modern::input_backend {

void BindLayout(const game::InputLayout* layout) noexcept;
const game::InputLayout* BoundLayout() noexcept;

void* Handler(void* app_main) noexcept;
bool PostPointer(void* handler, int x, int y, bool relative,
                 unsigned char action, unsigned char pressed) noexcept;
bool ReadCursorState(void* handler, bool* relative, int* command) noexcept;
void WriteCursorMode(void* handler, bool relative) noexcept;
unsigned char* CursorToggle(void* handler) noexcept;
void WriteCursorCommand(void* handler, unsigned int command) noexcept;

}  // namespace shim::versions::modern::input_backend
