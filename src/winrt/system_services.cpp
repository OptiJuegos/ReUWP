#include "shim/system_services.h"

#include "system_services_internal.h"

namespace shim::winrt {

Object* NetworkInformationStatics() noexcept {
  return system_detail::NetworkInformationSingleton();
}

Object* ResourceManagerStatics() noexcept {
  return system_detail::ResourceManagerSingleton();
}

Object* SpeechSynthesizerFactory() noexcept {
  return system_detail::SpeechFactorySingleton();
}

Object* SpeechSynthesizerStatics() noexcept {
  return system_detail::SpeechStaticsSingleton();
}

Object* HostNamesIterableProjection() noexcept {
  return system_detail::HostNamesIterableSingleton();
}

Object* ConnectionProfile2Projection() noexcept {
  return system_detail::ConnectionProfile2Singleton();
}

Object* ResourceContextForInterface(const GUID* iid) noexcept {
  return system_detail::ResolveResourceContext(iid);
}

}  // namespace shim::winrt
