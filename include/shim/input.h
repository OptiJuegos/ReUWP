#pragma once

#include "shim/common.h"

namespace shim::input {

enum class PointerAction : unsigned char {
  kMove = 0,
  kLeftButton = 1,
  kRightButton = 2,
  kMiddleButton = 3,
  kWheel = 4,
};

void SetAppMain(void* app_main) noexcept;
bool InstallHooks() noexcept;
bool InstallLifecycleHooks() noexcept;
bool RequestSubmit() noexcept;
void PumpSubmit() noexcept;

void SetEnabled(bool enabled) noexcept;
void* Handler() noexcept;

bool RegisterRawMouse(HWND window) noexcept;
void PostRawMouse(HRAWINPUT input) noexcept;

void RequestCursorMode(bool relative) noexcept;
bool CursorIsRelative() noexcept;
void SyncCursorMode(HWND window) noexcept;
void InvalidateCursorClip() noexcept;
void SetWindowInteractionActive(bool active) noexcept;
bool CursorGuardActive() noexcept;
void ArmCursorGuard() noexcept;
void ReleaseCursorGuard() noexcept;

void PostPointer(int x, int y, bool relative, PointerAction action,
                 unsigned char pressed) noexcept;
void PostMouseMove(LPARAM position) noexcept;
void PostKey(unsigned int virtual_key, bool down) noexcept;
void PostKeyEdge(unsigned int virtual_key, bool down) noexcept;
void PostChar(unsigned int codepoint) noexcept;
void FeedUtf16Unit(wchar_t unit) noexcept;
void ReleaseAllKeys() noexcept;
void SetSwallowNextChar() noexcept;
bool TextInputActive() noexcept;

}  // namespace shim::input
