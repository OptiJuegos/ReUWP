#include "shim/crypto_buffer.h"

#include "shim/log.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {
namespace {

// RtlGenRandom, que en advapi32 se exporta con el nombre SystemFunction036.
//
// Se resuelve por nombre y no se importa: la firma publica de la funcion no
// esta en ningun cabecero del SDK, y enlazarla estaticamente ataria el shim a
// advapi32 desde el arranque.
using GenRandomFn = BOOLEAN(WINAPI*)(void*, ULONG);
GenRandomFn g_gen_random = nullptr;
bool g_gen_random_resolved = false;  // byte_62998EE8

// CoTaskMemAlloc, resuelta perezosamente igual que en el original
// (dword_62998EEC). Solo hace falta para CopyToByteArray, que el juego casi
// nunca llama, y cargar ole32 por si acaso seria un coste sin motivo.
using CoTaskMemAllocFn = void*(WINAPI*)(SIZE_T);
CoTaskMemAllocFn g_co_task_mem_alloc = nullptr;

void FillRandom(unsigned char* destination, unsigned int size) noexcept {
  if (!g_gen_random_resolved) {
    g_gen_random_resolved = true;
    if (const HMODULE advapi = ::LoadLibraryW(L"advapi32.dll")) {
      g_gen_random = reinterpret_cast<GenRandomFn>(
          ::GetProcAddress(advapi, "SystemFunction036"));
    }
  }

  if (g_gen_random != nullptr && g_gen_random(destination, size)) {
    return;
  }

  // Reserva pobre pero suficiente: el juego usa esto para identificadores de
  // sesion, no para nada que deba resistir un ataque. Mezclar la hora con el
  // indice evita al menos que todos los bytes salgan iguales.
  SYSTEMTIME now = {};
  ::GetSystemTime(&now);
  unsigned int seed = (static_cast<unsigned int>(now.wMilliseconds) << 16) ^
                      (static_cast<unsigned int>(now.wSecond) << 8) ^
                      static_cast<unsigned int>(now.wMinute) ^
                      ::GetTickCount();
  for (unsigned int i = 0; i < size; ++i) {
    // Congruencial lineal, la misma constante que usa el CRT de MSVC.
    seed = seed * 1103515245u + 12345u;
    destination[i] = static_cast<unsigned char>(seed >> 16);
  }
}

// --- IBuffer --------------------------------------------------------------

// Los buffers NO son inmortales, a diferencia del resto de objetos del shim.
// Tienen su propio par AddRef/Release porque el del modulo base reinicia el
// contador a uno y nunca libera, y aqui eso seria una fuga por cada operacion
// criptografica.
ULONG SHIM_COM BufferAddRef(Buffer* self) noexcept {
  if (self == nullptr) {
    return 0;
  }
  return static_cast<ULONG>(::InterlockedIncrement(&self->ref_count));
}

ULONG SHIM_COM BufferRelease(Buffer* self) noexcept {
  if (self == nullptr) {
    return 0;
  }
  const LONG remaining = ::InterlockedDecrement(&self->ref_count);
  if (remaining == 0) {
    // Dos reservas, dos liberaciones, y en este orden: soltar la cabecera
    // primero dejaria el puntero a los bytes inalcanzable.
    const HANDLE heap = ::GetProcessHeap();
    ::HeapFree(heap, 0, self->data);
    ::HeapFree(heap, 0, self);
    return 0;
  }
  return static_cast<ULONG>(remaining);
}

HRESULT SHIM_COM BufferGetCapacity(Buffer* self, unsigned int* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = self != nullptr ? self->capacity : 0;
  return S_OK;
}

HRESULT SHIM_COM BufferGetLength(Buffer* self, unsigned int* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = self != nullptr ? self->length : 0;
  return S_OK;
}

HRESULT SHIM_COM BufferPutLength(Buffer* self, unsigned int value) noexcept {
  if (self == nullptr) {
    return E_POINTER;
  }
  // La longitud puede bajar pero no pasar de la capacidad: el buffer de bytes
  // ya esta reservado y no se redimensiona.
  if (value > self->capacity) {
    return E_INVALIDARG;
  }
  self->length = value;
  return S_OK;
}

// --- IBufferByteAccess ----------------------------------------------------
//
// Sus metodos reciben un puntero al offset 24 del objeto, no al principio. El
// puntero de vuelta del offset 28 es lo que permite recuperar el objeto entero.

Buffer* FromByteAccess(void* interface_pointer) noexcept {
  if (interface_pointer == nullptr) {
    return nullptr;
  }
  // El puntero de vuelta esta justo detras de la vtable de esta interfaz.
  return *reinterpret_cast<Buffer**>(
      static_cast<unsigned char*>(interface_pointer) + sizeof(void*));
}

HRESULT SHIM_COM ByteAccessQueryInterface(void* self, const GUID* iid,
                                          void** out) noexcept {
  (void)iid;
  if (out == nullptr) {
    return E_POINTER;
  }
  // Se devuelve SIEMPRE el objeto principal, no esta subinterfaz: quien haga QI
  // sobre IBufferByteAccess busca el IBuffer, no otra vista de los bytes.
  Buffer* buffer = FromByteAccess(self);
  if (buffer == nullptr) {
    *out = nullptr;
    return E_NOINTERFACE;
  }
  *out = buffer;
  BufferAddRef(buffer);
  return S_OK;
}

ULONG SHIM_COM ByteAccessAddRef(void* self) noexcept {
  return BufferAddRef(FromByteAccess(self));
}

ULONG SHIM_COM ByteAccessRelease(void* self) noexcept {
  return BufferRelease(FromByteAccess(self));
}

// IBufferByteAccess::Buffer: entrega el puntero crudo.
HRESULT SHIM_COM ByteAccessGetBuffer(void* self,
                                     unsigned char** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  Buffer* buffer = FromByteAccess(self);
  if (buffer == nullptr) {
    *out = nullptr;
    return E_POINTER;
  }
  *out = buffer->data;
  return S_OK;
}

Method g_buffer_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&BufferAddRef),
    reinterpret_cast<Method>(&BufferRelease),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&BufferGetCapacity),
    reinterpret_cast<Method>(&BufferGetLength),
    reinterpret_cast<Method>(&BufferPutLength),
};

// IBufferByteAccess deriva de IUnknown, NO de IInspectable: solo tres metodos
// de COM clasico antes del suyo.
Method g_byte_access_vtable[] = {
    reinterpret_cast<Method>(&ByteAccessQueryInterface),
    reinterpret_cast<Method>(&ByteAccessAddRef),
    reinterpret_cast<Method>(&ByteAccessRelease),
    reinterpret_cast<Method>(&ByteAccessGetBuffer),
};

// --- Estaticos de CryptographicBuffer --------------------------------------

// Compare (sub_6294BAA0): compara por LONGITUD, no por contenido.
//
// Es lo que hace el original: mira el campo del offset 16 de cada objeto. Es una
// comparacion incorrecta para criptografia, pero el juego solo la usa para
// descartar buffers obviamente distintos y reproducirla cuesta lo mismo que
// no hacerlo.
HRESULT SHIM_COM CompareBuffers(Object* self, Buffer* left, Buffer* right,
                                unsigned char* equal) noexcept {
  (void)self;
  if (left == nullptr || right == nullptr || equal == nullptr) {
    return E_POINTER;
  }
  *equal = left->capacity == right->capacity ? 1 : 0;
  return S_OK;
}

HRESULT SHIM_COM GenerateRandom(Object* self, unsigned int size,
                                void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = CreateBuffer(size, nullptr);
  return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

// GenerateRandomNumber (sub_6294BB80): un buffer de 4 bytes, se lee y se tira.
//
// Dar el rodeo por un buffer en vez de llamar al generador directamente es lo
// que hace el original, y sale gratis.
HRESULT SHIM_COM GenerateRandomNumber(Object* self,
                                      unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  Buffer* buffer = CreateBuffer(sizeof(unsigned int), nullptr);
  if (buffer == nullptr) {
    return E_OUTOFMEMORY;
  }
  *out = *reinterpret_cast<unsigned int*>(buffer->data);
  BufferRelease(buffer);
  return S_OK;
}

HRESULT SHIM_COM CreateFromByteArray(Object* self, unsigned int size,
                                     const void* source,
                                     void** out) noexcept {
  (void)self;
  if (out == nullptr || (source == nullptr && size != 0)) {
    return E_POINTER;
  }
  *out = CreateBuffer(size, source);
  return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

// CopyToByteArray (sub_6294BC40): saca los bytes a un array que libera el
// llamante.
//
// La memoria tiene que salir de CoTaskMemAlloc y no del monton del proceso: es
// quien la recibe (el marshalling de WinRT) el que la va a liberar, y lo hara
// con CoTaskMemFree.
HRESULT SHIM_COM CopyToByteArray(Object* self, Buffer* buffer,
                                 unsigned int* out_size,
                                 void** out_data) noexcept {
  (void)self;
  if (buffer == nullptr || out_size == nullptr || out_data == nullptr) {
    return E_POINTER;
  }
  *out_size = 0;
  *out_data = nullptr;

  if (g_co_task_mem_alloc == nullptr) {
    if (const HMODULE ole32 = ::LoadLibraryW(L"ole32.dll")) {
      g_co_task_mem_alloc = reinterpret_cast<CoTaskMemAllocFn>(
          ::GetProcAddress(ole32, "CoTaskMemAlloc"));
    }
    if (g_co_task_mem_alloc == nullptr) {
      return E_NOTIMPL;
    }
  }

  // Se pide al menos un byte: CoTaskMemAlloc(0) puede devolver null y el
  // llamante lo leeria como fallo.
  const unsigned int size = buffer->length;
  void* copy = g_co_task_mem_alloc(size != 0 ? size : 1);
  if (copy == nullptr) {
    return E_OUTOFMEMORY;
  }
  if (size != 0) {
    memcpy(copy, buffer->data, size);
  }

  *out_size = size;
  *out_data = copy;
  return S_OK;
}

// ICryptographicBufferStatics, contrastada con 0x62993440: 17 entradas, con los
// seis ultimos huecos nulos en el original.
Method g_statics_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&CompareBuffers),
    reinterpret_cast<Method>(&GenerateRandom),
    reinterpret_cast<Method>(&GenerateRandomNumber),
    reinterpret_cast<Method>(&CreateFromByteArray),
    reinterpret_cast<Method>(&CopyToByteArray),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

// Etiqueta del singleton de estaticos (objeto 0x629976C8 del binario).
constexpr int kCryptographicBufferKind = 26;

Object g_statics = {g_statics_vtable, 1, kCryptographicBufferKind};

}  // namespace

Object* CryptographicBufferStatics() noexcept {
  return &g_statics;
}

Buffer* CreateBuffer(unsigned int size, const void* source) noexcept {
  const HANDLE heap = ::GetProcessHeap();

  auto* buffer = static_cast<Buffer*>(::HeapAlloc(heap, 0, sizeof(Buffer)));
  if (buffer == nullptr) {
    return nullptr;
  }
  memset(buffer, 0, sizeof(Buffer));

  // Nunca se reserva cero bytes: HeapAlloc(0) devuelve un puntero valido pero
  // no desreferenciable, y el codigo de arriba asume que `data` se puede leer.
  auto* data = static_cast<unsigned char*>(
      ::HeapAlloc(heap, 0, size != 0 ? size : 1));
  if (data == nullptr) {
    ::HeapFree(heap, 0, buffer);
    return nullptr;
  }

  buffer->vtable = g_buffer_vtable;
  buffer->ref_count = 1;
  buffer->kind = kBufferKind;
  buffer->length = size;
  buffer->capacity = size;
  buffer->data = data;
  buffer->byte_access = g_byte_access_vtable;
  buffer->self = buffer;

  if (source != nullptr) {
    memcpy(data, source, size);
  } else {
    FillRandom(data, size != 0 ? size : 1);
  }

  return buffer;
}

}  // namespace shim::winrt
