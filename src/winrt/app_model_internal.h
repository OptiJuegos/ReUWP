#pragma once

#include "shim/app_model.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {

extern Method g_activation_factory_vtable[];
HRESULT SHIM_COM GenericBoolFalse(Object* self, unsigned char* out) noexcept;

namespace app_model_detail {

Object* GenericInstance() noexcept;
Object* PackageInstance() noexcept;
Object* ApplicationDataInstance() noexcept;
Object* LauncherOptionsInstance() noexcept;

}  // namespace app_model_detail
}  // namespace shim::winrt
