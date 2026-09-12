#include "app_model_internal.h"

#include "shim/storage.h"

namespace shim::winrt {
namespace {

// --- Instancia generica ---------------------------------------------------
//
// Lo que produce cualquier fabrica que ActivateInstance no reconoce. Responde a
// todo con el valor mas inofensivo posible.

// Slots 7/8 del objeto generico, raw 0x6294AC90/0x6294ACB0.  El
// resultado es BOOLEAN (un byte), no BOOL/DWORD.
HRESULT SHIM_COM GenericBoolTrue(Object* self, unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}

// Slot 10, raw 0x6294ACD0. Es un EventRegistrationToken de 64 bits escrito por
// puntero en el tercer argumento; bajo x86 __stdcall la funcion debe hacer
// ret 0x0C, de ahi esta firma de tres parametros.
HRESULT SHIM_COM GenericEventToken(Object* self, void* handler,
                                   unsigned int* token) noexcept {
  (void)self;
  (void)handler;
  if (token == nullptr) {
    return E_POINTER;
  }
  token[0] = 0;
  token[1] = 0;
  return S_OK;
}


#define SHIM_IINSPECTABLE                              \
  reinterpret_cast<Method>(&QueryInterface),           \
      reinterpret_cast<Method>(&AddRef),               \
      reinterpret_cast<Method>(&Release),              \
      reinterpret_cast<Method>(&GetIids),              \
      reinterpret_cast<Method>(&GetRuntimeClassName),  \
      reinterpret_cast<Method>(&GetTrustLevel)

}  // namespace

HRESULT SHIM_COM GenericBoolFalse(Object* self, unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

Method g_activation_factory_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&ActivateInstance),
};

namespace {

// Instancia generica (0x6299312C), 16 slots exactos. Los NULL se mantienen
// literalmente porque el original no instala ningun metodo en esas posiciones;
// rellenarlos con un stub de otra aridad puede ocultar una llamada ABI invalida.
Method g_generic_instance_vtable[] = {
    SHIM_IINSPECTABLE,                          // 0..5
    nullptr,                                    // 6
    reinterpret_cast<Method>(&GenericBoolTrue), // 7  0x6294AC90
    reinterpret_cast<Method>(&GenericBoolFalse),// 8  0x6294ACB0
    nullptr,                                    // 9
    reinterpret_cast<Method>(&GenericEventToken), // 10 0x6294ACD0
    nullptr,                                    // 11
    nullptr,                                    // 12
    nullptr,                                    // 13
    nullptr,                                    // 14
    nullptr,                                    // 15
};
static_assert(CountOf(g_generic_instance_vtable) == 16,
              "generic instance must match original 16-slot vtable");

Object g_generic_instance = {g_generic_instance_vtable, 1, kGenericInstance};

}  // namespace

namespace app_model_detail {

Object* GenericInstance() noexcept { return &g_generic_instance; }

}  // namespace app_model_detail

HRESULT SHIM_COM ActivateInstance(Object* self, void** instance) noexcept {
  if (instance == nullptr) {
    return E_POINTER;
  }

  Object* produced = app_model_detail::GenericInstance();
  if (self != nullptr) {
    switch (self->kind) {
      case kPackageFactory:
        produced = app_model_detail::PackageInstance();
        break;
      case kApplicationDataFactory:
        produced = app_model_detail::ApplicationDataInstance();
        break;
      case kLauncherOptionsFactory:
        produced = app_model_detail::LauncherOptionsInstance();
        break;
      default:
        if (Object* picker = storage::PickerInstance(self->kind)) {
          produced = picker;
        }
        break;
    }
  }

  return ReturnSingleton(produced, instance);
}

#undef SHIM_IINSPECTABLE

}  // namespace shim::winrt
