#pragma once

#include "shim/system_services.h"

namespace shim::winrt::system_detail {

inline constexpr HRESULT kBoundsError = static_cast<HRESULT>(0x8000000BL);

Object* HostNameSingleton() noexcept;
Object* HostNamesIterableSingleton() noexcept;
Object* ConnectionProfile2Singleton() noexcept;
Object* NetworkInformationSingleton() noexcept;

Object* VoiceInformationSingleton() noexcept;
Object* SpeechFactorySingleton() noexcept;
Object* SpeechStaticsSingleton() noexcept;

Object* ResourceManagerSingleton() noexcept;
Object* ResolveResourceContext(const GUID* iid) noexcept;

}  // namespace shim::winrt::system_detail
