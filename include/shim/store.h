// Windows.ApplicationModel.Store falso.
//
// Original: sub_6294A9F0, sub_6294AAA0, sub_6294AB10, sub_6294AB80 (estaticos de
// CurrentApp), sub_6294ACE0 (colecciones vacias) y sub_6294C180 (GetResults
// compartido).
//
// El juego consulta la tienda al arrancar: licencia, listado de productos y
// consumibles pendientes. Nada de eso existe fuera de la Store, asi que el shim
// responde con exito y contenido vacio. Fallar haria que el juego se creyera sin
// licencia y se cerrara.

#pragma once

#include "shim/winrt_object.h"

namespace shim::store {

// Los objetos de la tienda son objetos WinRT corrientes; shim::winrt es un
// espacio hermano, asi que el tipo hay que traerlo explicitamente.
using winrt::Object;

// Etiquetas de tipo (campo `kind`, en +8) de los objetos de este modulo.
//
// No son decorativas: hay metodos de vtable COMPARTIDOS entre varios tipos que
// miran este campo para saber a quien sirven. Ver ResultKind mas abajo.
enum Kind : int {
  kLicenseInformation = 9,
};

// Valores de `kind` que reconoce el GetResults compartido.
//
// El original tiene una unica implementacion de GetResults para todas las
// operaciones asincronas y despacha con un switch sobre este campo. Es la razon
// de que 454 funciones basten para lo que parecen docenas de clases.
//
// Todos estos valores estan LEIDOS del binario (el switch de sub_6294C180), no
// inferidos.
enum ResultKind : int {
  kReceiptOperation = 29,
  kUnfulfilledConsumablesOperation = 30,
  kSpeechSynthesisOperation = 53,

  // Operacion asincrona que devuelve un booleano en vez de un puntero. Escribe
  // UN BYTE, no una palabra: el resultado de un IAsyncOperation<bool> viaja como
  // `boolean`, que en la ABI de WinRT es de un byte.
  kBooleanOperation = 70,

  // Operacion asincrona de los dialogos de fichero. Devuelve el StorageFile que
  // eligio el usuario, o null si el dialogo se cancelo.
  kFilePickerOperation = 77,

  // Operacion asincrona que siempre devuelve null. Existe para que el juego
  // tenga algo que esperar donde el shim no puede dar nada.
  kNullResultOperation = 81,
};

// Recibo de compra que se devuelve siempre.
//
// XML vacio pero bien formado: el juego lo parsea, y una cadena vacia o nula lo
// haria fallar. Con un documento valido y sin compras dentro, concluye que no
// hay nada que restaurar, que es justo lo que se quiere.
inline constexpr wchar_t kEmptyReceipt[] = L"<Receipt />";
inline constexpr UINT32 kEmptyReceiptLength = 11;

// CurrentApp::LicenseInformation (sub_6294A9F0)
HRESULT SHIM_COM GetLicenseInformation(Object* self, void** out) noexcept;

// CurrentApp::RequestProductPurchaseAsync (sub_6294AAA0)
HRESULT SHIM_COM RequestReceiptAsync(Object* self, void** operation) noexcept;

// CurrentApp::LoadListingInformationAsync (sub_6294AB10)
HRESULT SHIM_COM LoadListingInformationAsync(Object* self,
                                             void** operation) noexcept;

// CurrentApp::GetUnfulfilledConsumablesAsync (sub_6294AB80)
HRESULT SHIM_COM GetUnfulfilledConsumablesAsync(Object* self,
                                               void** operation) noexcept;

// Coleccion vacia compartida (sub_6294ACE0).
//
// Sirve a LicenseInformation::ProductLicenses y a
// ListingInformation::ProductListings; distingue cual es por el `kind` del
// objeto que la invoca, solo para elegir el mensaje de traza.
HRESULT SHIM_COM GetEmptyCollection(Object* self, void** out) noexcept;

// Operaciones asincronas ya terminadas, para quien solo necesita algo que
// devolver. Comparten la vtable de las de la tienda y se distinguen por su
// `kind`, que es lo que lee GetResults.
//
// La booleana entrega `true`; la nula entrega null. Las usan Launcher y
// CachedFileManager, que hacen su trabajo de forma sincrona pero tienen que
// devolver algo con forma de operacion. ThreadPool usa un singleton kind=28
// distinto, reconstruido en app_model.cpp.
Object* BooleanOperation() noexcept;
Object* NullResultOperation() noexcept;

// La de sintesis de voz. Su GetResults entrega un stream NULO a proposito: el
// juego comprueba el stream y se salta la reproduccion si no lo hay, que es
// justo lo que se busca en Windows 7.
Object* SpeechOperation() noexcept;

// Singleton kind=28 compartido por CurrentApp::LoadListingInformationAsync y
// ThreadPool::RunAsync. El original usa literalmente 0x629976D4 en ambos
// caminos; conservar la identidad evita fabricar dos objetos equivalentes pero
// distintos.
Object* ListingOperation() noexcept;

// IAsyncOperation::put_Completed compartido (sub_6294C0B0).
//
// NO es un hueco vacio: invoca el manejador de finalizacion en el acto. Todas
// las operaciones del shim ya estan terminadas cuando se devuelven, asi que
// registrar el callback y llamarlo es la misma cosa. Sin esto el juego se queda
// esperando una notificacion que no va a llegar.
HRESULT SHIM_COM SetAsyncCompletedHandler(Object* self, void* handler) noexcept;

// IAsyncOperation::get_Completed compartido (sub_6294C160).
//
// Devuelve null porque el manejador no se guarda: se invoca y se olvida. El
// juego no vuelve a consultarlo.
HRESULT SHIM_COM GetAsyncCompletedHandler(Object* self, void** out) noexcept;

// GetResults compartido de las operaciones asincronas (sub_6294C180).
HRESULT SHIM_COM GetAsyncResults(Object* self, void** out) noexcept;

}  // namespace shim::store
