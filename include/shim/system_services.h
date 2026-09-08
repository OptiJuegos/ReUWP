// Red, recursos y sintesis de voz falsificados sobre Win32.
//
// Original: sub_6294B070..B240 (NetworkInformation), sub_6294B7F0..B8B0
// (HostName), sub_6294DA90..DB50 (ResourceManager) y sub_6294CEA0..CF80
// (SpeechSynthesizer de Windows 7).
//
//
// EL VECTOR DE UN SOLO ELEMENTO
//
// Cuatro clases distintas de este modulo tienen exactamente la misma vtable de
// coleccion: GetAt, get_Size, IndexOf y GetMany, sobre un unico elemento. En el
// binario son cuatro copias del mismo codigo con distinto elemento; aqui es una
// sola implementacion que saca el elemento del `kind` del objeto.
//
// Es el patron que se repite por todo el shim: al juego le basta con que la
// coleccion no este vacia, nunca recorre mas de un elemento.

#pragma once

#include "shim/winrt_object.h"

namespace shim::winrt {

// Etiquetas leidas de la tabla de singletons del binario.
enum SystemKind : int {
  kConnectionProfile = 12,
  kConnectionProfile2 = 13,
  kConnectionCost = 14,
  kEmptyCollection = 15,
  kHostNameVector = 18,
  kHostName = 21,
  kNetworkInformation = 25,

  // Sintesis de voz. La fabrica produce el sintetizador; los estaticos dan la
  // lista de voces instaladas, que es un vector de una sola.
  kSpeechSynthesizerFactory = 43,
  kSpeechVoiceStatics = 44,
  kSpeechSynthesizer = 45,
  kSpeechSynthesizerClosable = 46,
  kSpeechSynthesizerOptionsView = 47,
  kSpeechSynthesizerOptions = 48,
  kVoiceInformation = 49,
  kSpeechVoiceVector = 50,
  kSpeechVoiceIterable = 51,
  kSpeechVoiceIterator = 52,

  // Familia de ResourceContext. Los cinco comparten el QueryInterface
  // selectivo, que es lo que los distingue entre si.
  kResourceContext = 54,
  kResourceContextAlt1 = 55,
  kResourceContextAlt2 = 56,
  kResourceContextAlt3 = 57,
  kResourceContextAlt4 = 58,
  kResourceQualifierMap = 59,
  kResourceQualifierMapAlt1 = 60,
  kResourceQualifierMapAlt2 = 61,
  kResourceManager = 62,
  kResourceMap = 63,
  kResourceMapCollection = 64,
};

// Fabricas y estaticos para la tabla de activacion.
Object* NetworkInformationStatics() noexcept;
Object* ResourceManagerStatics() noexcept;
Object* SpeechSynthesizerFactory() noexcept;
Object* SpeechSynthesizerStatics() noexcept;
Object* HostNamesIterableProjection() noexcept;
Object* ConnectionProfile2Projection() noexcept;

// ResourceContext (sub_6294D360 mas el respaldo de la tabla de activacion).
//
// A diferencia del resto, esta clase NO se resuelve solo por nombre: el original
// hace un QueryInterface selectivo sobre el objeto base y, si el IID pedido no
// es ninguno de los cuatro que conoce, entrega un objeto de respaldo.
Object* ResourceContextForInterface(const GUID* iid) noexcept;

// QueryInterface selectivo de la familia de recursos (sub_6294D360).
//
// Esta familia usa su propio selector de IID porque un mismo runtime class se
// representa con varias proyecciones 54..64. El QueryInterface compartido tiene
// ademas sus proyecciones especiales de red/colecciones/async, pero no conoce
// esta familia de ResourceContext.
HRESULT SHIM_COM ResourceQueryInterface(Object* self, const GUID* iid,
                                        void** out) noexcept;

// ---------------------------------------------------------------------------
// IVectorView<T> de un solo elemento, compartido
//
// Las cuatro operaciones que implementa el binario. El elemento lo decide
// SingleElementOf() a partir del `kind` del vector.
// ---------------------------------------------------------------------------

// IVectorView::GetAt (sub_6294B7F0, sub_6294D170)
//
// Devuelve E_BOUNDS para cualquier indice que no sea 0. Es el codigo que WinRT
// espera; un E_FAIL haria que el juego lo tratara como error real.
HRESULT SHIM_COM VectorGetAt(Object* self, unsigned int index,
                             void** out) noexcept;

// IVectorView::get_Size (sub_6294B840, sub_6294D1B0): siempre 1.
HRESULT SHIM_COM VectorGetSize(Object* self, unsigned int* out) noexcept;

// IVectorView::IndexOf (sub_6294B870, sub_6294D1D0)
HRESULT SHIM_COM VectorIndexOf(Object* self, Object* item, unsigned int* index,
                               unsigned char* found) noexcept;

// IVectorView::GetMany (sub_6294B8B0, sub_6294D210)
HRESULT SHIM_COM VectorGetMany(Object* self, unsigned int start,
                               unsigned int capacity, void** items,
                               unsigned int* written) noexcept;

}  // namespace shim::winrt
