#include "shim/activation.h"

#include "shim/winrt_runtime.h"

#include <hstring.h>
#include <inspectable.h>

extern "C" {

HRESULT SHIM_COM RoGetActivationFactory(HSTRING activatable_class_id,
                                        REFIID iid, void** factory) {
  if (factory == nullptr) {
    return E_POINTER;
  }
  *factory = nullptr;

  const HRESULT apartment = shim::winrt::CheckApartmentInitialized();
  if (FAILED(apartment)) {
    return apartment;
  }

  return shim::winrt::GetActivationFactoryByHString(
      activatable_class_id, &iid, factory);
}

HRESULT SHIM_COM RoActivateInstance(HSTRING activatable_class_id,
                                    IInspectable** instance) {
  if (instance == nullptr) {
    return E_POINTER;
  }
  *instance = nullptr;

  const HRESULT apartment = shim::winrt::CheckApartmentInitialized();
  if (FAILED(apartment)) {
    return apartment;
  }

  return shim::winrt::ActivateInstanceByHString(
      activatable_class_id, reinterpret_cast<void**>(instance));
}

}  // extern "C"
