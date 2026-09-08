#include "shim/system_services.h"
#include "shim/winrt_string_compat.h"

#include "system_services_internal.h"

#include <winstring.h>

#include "shim/store.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {
namespace {

// --- SpeechSynthesizer ----------------------------------------------------
//
// La familia Speech (kinds 43..53) usa QueryInterface propio en el original
// (0x6294CCC0). Esto es critico: el mismo runtime object tiene proyecciones
// IClosable/options/vector/iterable/iterator con vtables de distinto largo.

extern Object g_speech_factory;
extern Object g_speech_statics;
extern Object g_speech_synthesizer;
extern Object g_speech_projection46;
extern Object g_speech_projection47;
extern Object g_speech_projection48;
extern Object g_voice_information;
extern Object g_speech_voice_vector;
extern Object g_speech_projection51;
extern Object g_speech_projection52;

constexpr GUID kSpeechIidUnknown = {
    0x00000000, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kSpeechIidInspectable = {
    0xAF86E2E0, 0xB12D, 0x4C6A,
    {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
constexpr GUID kSpeechIidAgileObject = {
    0x94EA2B94, 0xE9CC, 0x49E0,
    {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
constexpr GUID kSpeechIidActivationFactory = {
    0x00000035, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kSpeechIidStatics = {
    0x7D526ECC, 0x7533, 0x4C3F,
    {0x85, 0xBE, 0x88, 0x8C, 0x2B, 0xAE, 0xEB, 0xDC}};
constexpr GUID kSpeechIidProjection46 = {
    0x30D5A829, 0x7FA4, 0x4026,
    {0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E}};
constexpr GUID kSpeechIidProjection47 = {
    0xA7C5ECB2, 0x4339, 0x4D6A,
    {0xBB, 0xF8, 0xC7, 0xA4, 0xF1, 0x54, 0x4C, 0x2E}};
constexpr GUID kSpeechIidSynthesizer = {
    0xCE9F7C76, 0x97F4, 0x4CED,
    {0xAD, 0x68, 0xD5, 0x1C, 0x45, 0x8E, 0x45, 0xC6}};
constexpr GUID kSpeechIidOptions = {
    0xA0E23871, 0xCC3D, 0x43C9,
    {0x91, 0xB1, 0xEE, 0x18, 0x53, 0x24, 0xD8, 0x3D}};
constexpr GUID kSpeechIidVoiceInfo = {
    0xB127D6A4, 0x1291, 0x4604,
    {0xAA, 0x9C, 0x83, 0x13, 0x40, 0x83, 0x35, 0x2C}};
constexpr GUID kSpeechIidVoiceIterable = {
    0x3C33BB52, 0xBD98, 0x5C8C,
    {0xAD, 0xEE, 0xEE, 0x8D, 0xA0, 0x62, 0x8E, 0xFC}};
constexpr GUID kSpeechIidVoiceIterator = {
    0x12D40A27, 0xAE8D, 0x5FB0,
    {0x8F, 0xED, 0x00, 0x16, 0x5D, 0x59, 0xC6, 0xAB}};
constexpr GUID kSpeechIidVoiceVector = {
    0xEE8D63CE, 0x51AC, 0x5984,
    {0x89, 0x1B, 0xD2, 0x32, 0xFA, 0x7F, 0x64, 0x53}};

bool SameSpeechGuid(const GUID* a, const GUID& b) noexcept {
  return a != nullptr && memcmp(a, &b, sizeof(GUID)) == 0;
}

HRESULT SHIM_COM SpeechQueryInterface(Object* self, const GUID* iid,
                                      void** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  if (self == nullptr || iid == nullptr) {
    return E_NOINTERFACE;
  }

  Object* target = nullptr;
  switch (self->kind) {
    case kSpeechSynthesizerFactory:
    case kSpeechVoiceStatics:
      if (SameSpeechGuid(iid, kSpeechIidStatics)) {
        target = &g_speech_statics;
      } else if (SameSpeechGuid(iid, kSpeechIidActivationFactory)) {
        target = &g_speech_factory;
      }
      break;

    case kSpeechSynthesizer:
    case kSpeechSynthesizerClosable:
    case kSpeechSynthesizerOptionsView:
      if (SameSpeechGuid(iid, kSpeechIidProjection46)) {
        target = &g_speech_projection46;
      } else if (SameSpeechGuid(iid, kSpeechIidProjection47)) {
        target = &g_speech_projection47;
      } else if (SameSpeechGuid(iid, kSpeechIidSynthesizer)) {
        target = &g_speech_synthesizer;
      }
      break;

    case kSpeechSynthesizerOptions:
      if (SameSpeechGuid(iid, kSpeechIidOptions)) {
        target = &g_speech_projection48;
      }
      break;

    case kVoiceInformation:
      if (SameSpeechGuid(iid, kSpeechIidVoiceInfo)) {
        target = &g_voice_information;
      }
      break;

    case kSpeechVoiceVector:
    case kSpeechVoiceIterable:
    case kSpeechVoiceIterator:
      if (SameSpeechGuid(iid, kSpeechIidVoiceIterable)) {
        target = &g_speech_projection51;
      } else if (SameSpeechGuid(iid, kSpeechIidVoiceIterator)) {
        target = &g_speech_projection52;
      } else if (SameSpeechGuid(iid, kSpeechIidVoiceVector)) {
        target = &g_speech_voice_vector;
      }
      break;

    default:
      break;
  }

  // El final comun de 0x6294CCC0 acepta IUnknown/IInspectable/IAgileObject y
  // conserva la proyeccion desde la que se hizo la consulta.
  if (target == nullptr &&
      (SameSpeechGuid(iid, kSpeechIidUnknown) ||
       SameSpeechGuid(iid, kSpeechIidInspectable) ||
       SameSpeechGuid(iid, kSpeechIidAgileObject))) {
    target = self;
  }

  if (target == nullptr) {
    return E_NOINTERFACE;
  }
  return ReturnSingleton(target, out);
}

#define SHIM_SPEECH_IINSPECTABLE                       \
  reinterpret_cast<Method>(&SpeechQueryInterface),     \
      reinterpret_cast<Method>(&AddRef),               \
      reinterpret_cast<Method>(&Release),              \
      reinterpret_cast<Method>(&GetIids),              \
      reinterpret_cast<Method>(&GetRuntimeClassName),  \
      reinterpret_cast<Method>(&GetTrustLevel)

HRESULT SHIM_COM GetVoiceDisplayName(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"ReUWP Win32 Voice", 17, out);
}

HRESULT SHIM_COM GetVoiceLanguage(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"en-US", 5, out);
}

HRESULT SHIM_COM GetVoiceEnum(Object* self, int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

Method g_voice_information_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&GetVoiceDisplayName),
    reinterpret_cast<Method>(&GetVoiceDisplayName),
    reinterpret_cast<Method>(&GetVoiceLanguage),
    reinterpret_cast<Method>(&GetVoiceDisplayName),
    reinterpret_cast<Method>(&GetVoiceEnum),
};
static_assert(CountOf(g_voice_information_vtable) == 11,
              "VoiceInformation must match original 11-slot vtable");
Object g_voice_information = {g_voice_information_vtable, 1,
                              kVoiceInformation};

HRESULT SHIM_COM SynthesizeTextToStreamAsync(Object* self, HSTRING text,
                                             void** operation) noexcept {
  (void)self;
  (void)text;
  if (operation == nullptr) {
    return E_POINTER;
  }
  return ReturnSingleton(store::SpeechOperation(), operation);
}

HRESULT SHIM_COM SetSpeechVoice(Object* self, Object* voice) noexcept {
  (void)self;
  (void)voice;
  return S_OK;
}

HRESULT SHIM_COM GetSpeechVoice(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_voice_information, out);
}

Method g_speech_synthesizer_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&SynthesizeTextToStreamAsync),
    reinterpret_cast<Method>(&SynthesizeTextToStreamAsync),
    reinterpret_cast<Method>(&SetSpeechVoice),
    reinterpret_cast<Method>(&GetSpeechVoice),
};
static_assert(CountOf(g_speech_synthesizer_vtable) == 10,
              "SpeechSynthesizer must match original 10-slot vtable");
Object g_speech_synthesizer = {g_speech_synthesizer_vtable, 1,
                               kSpeechSynthesizer};

// kind 46, raw 0x6299374C: IClosable::Close, ret 4.
HRESULT SHIM_COM SpeechClose(Object* self) noexcept {
  (void)self;
  return S_OK;
}
Method g_speech_projection46_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&SpeechClose),
};
static_assert(CountOf(g_speech_projection46_vtable) == 7,
              "Speech projection 46 must have 7 slots");
Object g_speech_projection46 = {g_speech_projection46_vtable, 1,
                                kSpeechSynthesizerClosable};

// kind 48 options object, raw 0x62993784.
HRESULT SHIM_COM SpeechOptionFalse(Object* self, unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}
HRESULT SHIM_COM SpeechOptionSet(Object* self, unsigned char value) noexcept {
  (void)self;
  (void)value;
  return S_OK;
}
Method g_speech_projection48_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&SpeechOptionFalse),
    reinterpret_cast<Method>(&SpeechOptionSet),
    reinterpret_cast<Method>(&SpeechOptionFalse),
    reinterpret_cast<Method>(&SpeechOptionSet),
};
static_assert(CountOf(g_speech_projection48_vtable) == 10,
              "Speech projection 48 must have 10 slots");
Object g_speech_projection48 = {g_speech_projection48_vtable, 1,
                                kSpeechSynthesizerOptions};

// kind 47: getter que entrega la proyeccion options kind 48.
HRESULT SHIM_COM GetSpeechOptions(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_speech_projection48, out);
}
Method g_speech_projection47_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&GetSpeechOptions),
};
static_assert(CountOf(g_speech_projection47_vtable) == 7,
              "Speech projection 47 must have 7 slots");
Object g_speech_projection47 = {g_speech_projection47_vtable, 1,
                                kSpeechSynthesizerOptionsView};

// kind 50: IVectorView<VoiceInformation>, pero con SpeechQueryInterface para
// poder proyectarlo a IIterable/IIterator (kinds 51/52).
Method g_speech_voice_vector_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&VectorGetAt),
    reinterpret_cast<Method>(&VectorGetSize),
    reinterpret_cast<Method>(&VectorIndexOf),
    reinterpret_cast<Method>(&VectorGetMany),
};
static_assert(CountOf(g_speech_voice_vector_vtable) == 10,
              "speech voice vector must have 10 slots");
Object g_speech_voice_vector = {g_speech_voice_vector_vtable, 1,
                                kSpeechVoiceVector};

HRESULT SHIM_COM SpeechVoiceFirst(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_speech_projection52, out);
}
Method g_speech_projection51_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&SpeechVoiceFirst),
};
static_assert(CountOf(g_speech_projection51_vtable) == 7,
              "Speech projection 51 must have 7 slots");
Object g_speech_projection51 = {g_speech_projection51_vtable, 1,
                                kSpeechVoiceIterable};

HRESULT SHIM_COM SpeechVoiceCurrent(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_voice_information, out);
}
HRESULT SHIM_COM SpeechVoiceHasCurrent(Object* self,
                                       unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}
HRESULT SHIM_COM SpeechVoiceMoveNext(Object* self,
                                     unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}
HRESULT SHIM_COM SpeechVoiceIteratorGetMany(Object* self,
                                            unsigned int capacity,
                                            void** items,
                                            unsigned int* written) noexcept {
  (void)self;
  if (written == nullptr) {
    return E_POINTER;
  }
  *written = 0;
  if (capacity == 0 || items == nullptr) {
    return S_OK;
  }
  items[0] = &g_voice_information;
  AddRef(&g_voice_information);
  *written = 1;
  return S_OK;
}
Method g_speech_projection52_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&SpeechVoiceCurrent),
    reinterpret_cast<Method>(&SpeechVoiceHasCurrent),
    reinterpret_cast<Method>(&SpeechVoiceMoveNext),
    reinterpret_cast<Method>(&SpeechVoiceIteratorGetMany),
};
static_assert(CountOf(g_speech_projection52_vtable) == 10,
              "Speech projection 52 must have 10 slots");
Object g_speech_projection52 = {g_speech_projection52_vtable, 1,
                                kSpeechVoiceIterator};

HRESULT SHIM_COM GetInstalledVoices(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_speech_voice_vector, out);
}
HRESULT SHIM_COM GetDefaultVoice(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_voice_information, out);
}
Method g_speech_statics_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&GetInstalledVoices),
    reinterpret_cast<Method>(&GetDefaultVoice),
};
static_assert(CountOf(g_speech_statics_vtable) == 8,
              "Speech statics must have 8 slots");
Object g_speech_statics = {g_speech_statics_vtable, 1, kSpeechVoiceStatics};

HRESULT SHIM_COM CreateSpeechSynthesizer(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_speech_synthesizer, out);
}
Method g_speech_factory_vtable[] = {
    SHIM_SPEECH_IINSPECTABLE,
    reinterpret_cast<Method>(&CreateSpeechSynthesizer),
};
static_assert(CountOf(g_speech_factory_vtable) == 7,
              "Speech factory must have 7 slots");
Object g_speech_factory = {g_speech_factory_vtable, 1,
                           kSpeechSynthesizerFactory};

#undef SHIM_SPEECH_IINSPECTABLE


}  // namespace

namespace system_detail {

Object* VoiceInformationSingleton() noexcept { return &g_voice_information; }

Object* SpeechFactorySingleton() noexcept { return &g_speech_factory; }

Object* SpeechStaticsSingleton() noexcept { return &g_speech_statics; }

}  // namespace system_detail
}  // namespace shim::winrt
