#include "shim/system_services.h"

#include "system_services_internal.h"

namespace shim::winrt {
namespace {

Object* SingleElementOf(const Object* vector) noexcept {
  if (vector == nullptr) {
    return nullptr;
  }

  switch (vector->kind) {
    case kHostNameVector:
      return system_detail::HostNameSingleton();
    case kSpeechVoiceVector:
      return system_detail::VoiceInformationSingleton();
    default:
      return nullptr;
  }
}

}  // namespace

HRESULT SHIM_COM VectorGetAt(Object* self, unsigned int index,
                             void** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  if (index != 0) {
    return system_detail::kBoundsError;
  }

  Object* element = SingleElementOf(self);
  if (element == nullptr) {
    return system_detail::kBoundsError;
  }
  return ReturnSingleton(element, out);
}

HRESULT SHIM_COM VectorGetSize(Object* self, unsigned int* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = SingleElementOf(self) != nullptr ? 1u : 0u;
  return S_OK;
}

HRESULT SHIM_COM VectorIndexOf(Object* self, Object* item, unsigned int* index,
                               unsigned char* found) noexcept {
  if (index == nullptr || found == nullptr) {
    return E_POINTER;
  }
  *index = 0;
  *found = (item != nullptr && item == SingleElementOf(self)) ? 1 : 0;
  return S_OK;
}

HRESULT SHIM_COM VectorGetMany(Object* self, unsigned int start,
                               unsigned int capacity, void** items,
                               unsigned int* written) noexcept {
  if (written == nullptr) {
    return E_POINTER;
  }
  *written = 0;
  if (start != 0 || capacity == 0 || items == nullptr) {
    return S_OK;
  }

  Object* element = SingleElementOf(self);
  if (element == nullptr) {
    return S_OK;
  }

  items[0] = element;
  AddRef(element);
  *written = 1;
  return S_OK;
}

}  // namespace shim::winrt
