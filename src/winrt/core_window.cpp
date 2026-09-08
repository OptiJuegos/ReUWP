#include "shim/core_window.h"
#include "shim/runtime_trace.h"

#include "shim/log.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {
namespace {

// ---------------------------------------------------------------------------
// Cola del dispatcher
// ---------------------------------------------------------------------------

// Un manejador encolado con CoreDispatcher::RunAsync o RunIdleAsync.
//
// El binario guarda estos dos campos como un unico __int64 por ranura: el
// puntero en la palabra baja y el flag en la alta. Aqui van separados porque el
// empaquetado no aporta nada y el flag decide con que firma se invoca.
struct QueuedHandler {
  // IDispatchedHandler o IIdleDispatchedHandler del juego.
  void* handler;
  // True si vino por RunIdleAsync. Cambia la ARIDAD de la llamada, no solo el
  // mensaje de traza: ver ProcessQueuedHandlers.
  bool idle;
};

// Anillo, no pila: el binario avanza los indices con `& 0x3F`, que es lo que
// fija la capacidad en 64 y lo que hace que "cola llena" sea una condicion real
// y no una que nunca se da.
QueuedHandler g_queue[kDispatcherQueueCapacity];
unsigned g_queue_write = 0;
unsigned g_queue_read = 0;
volatile LONG g_queue_lock = 0;  // dword_62999110

constexpr unsigned kQueueMask = kDispatcherQueueCapacity - 1;
static_assert((kDispatcherQueueCapacity & kQueueMask) == 0,
              "La capacidad debe ser potencia de dos para que la mascara valga");

// sub_6294EC70.  La cola se toca desde el hilo de UI y desde callbacks que
// pueden llegar por ThreadPool, por eso el original usa un spinlock incluso
// siendo un anillo de solo 64 entradas.  Dormir con Sleep(0) cede el quantum
// sin convertir esta ruta corta en una espera kernel mas pesada.
void AcquireQueueLock() noexcept {
  while (::InterlockedExchange(&g_queue_lock, 1) != 0) {
    ::Sleep(0);
  }
}

void ReleaseQueueLock() noexcept {
  ::InterlockedExchange(&g_queue_lock, 0);
}

// Los tres metodos de IUnknown en la vtable de un delegado.
constexpr size_t kSlotAddRef = 1;
constexpr size_t kSlotRelease = 2;
constexpr size_t kSlotInvoke = 3;

using AddRefFn = ULONG(SHIM_COM*)(void*);
using ReleaseFn = ULONG(SHIM_COM*)(void*);

// Comprueba que el manejador tenga vtable legible y los tres huecos que se van a
// usar. El original hace exactamente estas comprobaciones antes de encolar.
void* const* HandlerVtable(void* handler) noexcept {
  if (handler == nullptr || ::IsBadReadPtr(handler, sizeof(void*))) {
    return nullptr;
  }
  void* const* vtable = *static_cast<void* const* const*>(handler);
  if (vtable == nullptr || ::IsBadReadPtr(vtable, 4 * sizeof(void*))) {
    return nullptr;
  }
  if (vtable[kSlotAddRef] == nullptr || vtable[kSlotRelease] == nullptr ||
      vtable[kSlotInvoke] == nullptr) {
    return nullptr;
  }
  return vtable;
}

void HandlerAddRef(void* handler, void* const* vtable) noexcept {
  reinterpret_cast<AddRefFn>(vtable[kSlotAddRef])(handler);
}

void HandlerRelease(void* handler) noexcept {
  if (void* const* vtable = HandlerVtable(handler)) {
    reinterpret_cast<ReleaseFn>(vtable[kSlotRelease])(handler);
  }
}

// Profundidad de reentrada del drenado.
//
// Un manejador puede encolar mas trabajo, e incluso llamar a ProcessEvents.
// Contarlo permite detectar recursion en la traza; el original lo saca como
// "depth" en su mensaje.
unsigned g_dispatch_depth = 0;

// ---------------------------------------------------------------------------
// Metodos de ICoreWindow
//
// Orden de la interfaz, empezando en el indice 6 (tras IInspectable):
//   6  get_AutomationHostProvider
//   7  get_Bounds
//   8  get_CustomProperties
//   9  get_Dispatcher
//
// Los tres primeros son los huecos que el original deja a NULL y aqui se
// rellenan con el stub que deja rastro. Ver winrt_vtable.h.
// ---------------------------------------------------------------------------

// ICoreWindow::get_Dispatcher (sub_6294EA60)
HRESULT SHIM_COM CoreWindow_get_Dispatcher(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(CoreDispatcherSingleton(), out);
  if (SUCCEEDED(result)) {
    log::Write("CoreWindow.Dispatcher redirected to HWND dispatcher");
  }
  return result;
}

// ---------------------------------------------------------------------------
// El resto de ICoreWindow
//
// ICoreWindow declara 58 slots: los 6 de IInspectable y 52 propios (contados
// sobre winrt/windows.ui.core.h del SDK). El original solo rellena hasta el 9 y
// deja el array ahi. Cualquier indice por encima lo resuelve el juego leyendo
// MAS ALLA del array, sobre lo que haya a continuacion en .data.
//
// En el binario original eso cae, por casualidad del layout, sobre un AddRef:
// devuelve el contador de referencias, que es positivo, el juego lo lee como
// HRESULT de exito y sigue. Aqui el vecino era la vtable de
// CryptographicBuffer, asi que el slot 36 (add_KeyUp) aterrizaba en
// CreateFromByteArray, que interpretaba el handler como TAMANO y el token del
// juego como ORIGEN:
//
//   memcpy(dst=heap, src=&pila_del_juego, count=0x0469C740)  -> ~70 MB
//
// El `rep movsb` se salia por el final de la pila y ahi moria 1.1.5. Confirmado
// en x32dbg: game+0x7AB80E `call [eax+0x90]`, fallo con ESI=0x001A4000
// (PAGE_NOACCESS).
//
// Por eso la vtable se declara ENTERA. No es defensa preventiva: es que el
// despacho por indice de COM no tiene forma de saber donde acaba el array, y el
// unico modo de que no se salga es que no falte ningun slot.
//
// Las aridades son parte de la ABI. Son metodos __stdcall, o sea que limpia la
// pila el llamado, y un stub con argumentos de menos descuadraria ESP aunque
// nunca tocara memoria invalida. De ahi que haya un stub por FORMA de metodo y
// no uno solo para todo.
// ---------------------------------------------------------------------------

// add_*(handler, EventRegistrationToken* token): 12 bytes de argumentos.
//
// Se acepta el alta y se devuelve un token cero, que es justo el efecto
// observable que producia el AddRef accidental del original. Ademas es lo
// correcto para este shim: la entrada no llega por eventos de CoreWindow, sino
// por la ruta Win32 de input.cpp, asi que no hay nada que registrar.
HRESULT SHIM_COM CoreWindow_AddEventStub(Object* self, void* handler,
                                         INT64* token) noexcept {
  (void)self;
  (void)handler;
  if (token != nullptr) {
    *token = 0;
  }
  log::Write("ICoreWindow event registration accepted as no-op");
  return S_OK;
}

// remove_*(EventRegistrationToken token): el token va POR VALOR y es un
// __int64, asi que tambien son 12 bytes. Coincide con el del alta por
// casualidad, no por parentesco; van separados para que se vea.
HRESULT SHIM_COM CoreWindow_RemoveEventStub(Object* self,
                                            INT64 token) noexcept {
  (void)self;
  (void)token;
  return S_OK;
}

// Getters que devuelven una interfaz: (this, void** out).
HRESULT SHIM_COM CoreWindow_GetInterfaceStub(Object* self,
                                             void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  log::Write("ICoreWindow interface getter stub returned null");
  return S_OK;
}

// get_Bounds(Rect*): cuatro float contiguos.
HRESULT SHIM_COM CoreWindow_GetBoundsStub(Object* self, float* rect) noexcept {
  (void)self;
  if (rect == nullptr) {
    return E_POINTER;
  }
  rect[0] = 0.0f;
  rect[1] = 0.0f;
  rect[2] = 0.0f;
  rect[3] = 0.0f;
  return S_OK;
}

// get_PointerPosition(Point*): dos float contiguos.
HRESULT SHIM_COM CoreWindow_GetPointStub(Object* self, float* point) noexcept {
  (void)self;
  if (point == nullptr) {
    return E_POINTER;
  }
  point[0] = 0.0f;
  point[1] = 0.0f;
  return S_OK;
}

// Getters de `boolean`, que en la ABI de WinRT es un byte.
//
// Se contesta 1 en los dos sitios donde se usa (IsInputEnabled y Visible):
// decir que la ventana esta oculta o la entrada desactivada haria que el juego
// se quedara esperando un evento que este shim no emite nunca.
HRESULT SHIM_COM CoreWindow_GetTrueStub(Object* self,
                                        unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}

// Getters de enum de 4 bytes (FlowDirection).
HRESULT SHIM_COM CoreWindow_GetZeroStub(Object* self, int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// Setters de un valor de 4 bytes: (this, valor). Vale para los `boolean`, los
// enum y los punteros a interfaz, que en x86 ocupan lo mismo en la pila.
HRESULT SHIM_COM CoreWindow_PutStub(Object* self, int value) noexcept {
  (void)self;
  (void)value;
  return S_OK;
}

// Metodos sin argumentos: Activate, Close y la captura de puntero.
HRESULT SHIM_COM CoreWindow_NoArgStub(Object* self) noexcept {
  (void)self;
  return S_OK;
}

// GetAsyncKeyState / GetKeyState: (this, VirtualKey, CoreVirtualKeyStates*).
//
// Cero es CoreVirtualKeyStates_None. El estado real del teclado lo lleva
// input.cpp y el juego lo consulta por su propia ruta, no por aqui.
HRESULT SHIM_COM CoreWindow_GetKeyStateStub(Object* self, int virtual_key,
                                            int* out) noexcept {
  (void)self;
  (void)virtual_key;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// ---------------------------------------------------------------------------
// Metodos de ICoreDispatcher
//
//   6  get_HasThreadAccess
//   7  ProcessEvents
//   8  RunAsync
//   9  RunIdleAsync
// ---------------------------------------------------------------------------

// ICoreDispatcher::get_HasThreadAccess (sub_6294EAD0)
//
// Siempre true. El shim corre todo en el hilo del bucle de mensajes, asi que
// desde el punto de vista del juego siempre esta "en el hilo de la UI"; devolver
// false le haria encolar trabajo que nadie iba a drenar.
HRESULT SHIM_COM Dispatcher_get_HasThreadAccess(Object* self,
                                                unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  // El `boolean` de la ABI de WinRT es un byte, no el bool de C++ ni un BOOL.
  *out = 1;
  return S_OK;
}

// ICoreDispatcher::ProcessEvents (sub_6294EAF0)
HRESULT SHIM_COM Dispatcher_ProcessEvents(Object* self, int options) noexcept {
  (void)self;
  (void)options;
  // Raw 0x6294EB10 is intentionally a no-op: the original host drains the
  // dispatcher queue from its 1.1.5 idle message-loop path, not from
  // ICoreDispatcher::ProcessEvents itself.
  return S_OK;
}

// Encola un manejador. Comun a RunAsync y RunIdleAsync (sub_6294EB40).
HRESULT QueueHandler(bool idle, void* handler, void** operation) noexcept {
  // raw 0x6294EB40 no encola nada si falta el parametro de salida.
  if (operation == nullptr) {
    return E_POINTER;
  }
  *operation = nullptr;

  void* const* vtable = HandlerVtable(handler);
  if (vtable == nullptr) {
    return E_POINTER;
  }

  // Se toma la referencia ANTES de comprobar si cabe, y se suelta si no cabe.
  // El orden importa: entre encolar y drenar, el juego suelta su propia
  // referencia, y sin esta el manejador estaria destruido cuando le toque
  // ejecutarse.
  HandlerAddRef(handler, vtable);

  AcquireQueueLock();
  const unsigned next = (g_queue_write + 1) & kQueueMask;
  if (next == g_queue_read) {
    // El original libera el lock ANTES de soltar la referencia del delegado.
    ReleaseQueueLock();
    HandlerRelease(handler);
    log::Write("CoreDispatcher queue full; handler rejected");
    return E_FAIL;
  }

  g_queue[g_queue_write].handler = handler;
  g_queue[g_queue_write].idle = idle;
  g_queue_write = next;
  ReleaseQueueLock();

  log::Writef("CoreDispatcher.%s queued handler=%p invoke=%p depth=%u",
              idle ? "RunIdleAsync" : "RunAsync", handler,
              vtable[kSlotInvoke], g_dispatch_depth);
  return S_OK;
}

// ICoreDispatcher::RunAsync (sub_6294EB00)
HRESULT SHIM_COM Dispatcher_RunAsync(Object* self, int priority, void* handler,
                                     void** operation) noexcept {
  (void)self;
  (void)priority;
  return QueueHandler(false, handler, operation);
}

// ICoreDispatcher::RunIdleAsync (sub_6294EB20)
HRESULT SHIM_COM Dispatcher_RunIdleAsync(Object* self, void* handler,
                                         void** operation) noexcept {
  (void)self;
  return QueueHandler(true, handler, operation);
}

// ---------------------------------------------------------------------------
// Vtables
//
// Deliberadamente NO const: viven en datos mutables igual que en el original
// (off_62993CC0 y off_62993CE8 estan en .data), porque algunas se parchean segun
// la version del juego.
// ---------------------------------------------------------------------------

// ICoreWindow al completo. El orden es el del .idl y NO es negociable: el juego
// llama por indice, y el indice que importa aqui es el 36.
Method g_core_window_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),                 //  0
    reinterpret_cast<Method>(&AddRef),                         //  1
    reinterpret_cast<Method>(&Release),                        //  2
    reinterpret_cast<Method>(&GetIids),                        //  3
    reinterpret_cast<Method>(&GetRuntimeClassName),            //  4
    reinterpret_cast<Method>(&GetTrustLevel),                  //  5
    reinterpret_cast<Method>(&CoreWindow_GetInterfaceStub),    //  6 get_AutomationHostProvider
    reinterpret_cast<Method>(&CoreWindow_GetBoundsStub),       //  7 get_Bounds
    reinterpret_cast<Method>(&CoreWindow_GetInterfaceStub),    //  8 get_CustomProperties
    reinterpret_cast<Method>(&CoreWindow_get_Dispatcher),      //  9 get_Dispatcher
    reinterpret_cast<Method>(&CoreWindow_GetZeroStub),         // 10 get_FlowDirection
    reinterpret_cast<Method>(&CoreWindow_PutStub),             // 11 put_FlowDirection
    reinterpret_cast<Method>(&CoreWindow_GetTrueStub),         // 12 get_IsInputEnabled
    reinterpret_cast<Method>(&CoreWindow_PutStub),             // 13 put_IsInputEnabled
    reinterpret_cast<Method>(&CoreWindow_GetInterfaceStub),    // 14 get_PointerCursor
    reinterpret_cast<Method>(&CoreWindow_PutStub),             // 15 put_PointerCursor
    reinterpret_cast<Method>(&CoreWindow_GetPointStub),        // 16 get_PointerPosition
    reinterpret_cast<Method>(&CoreWindow_GetTrueStub),         // 17 get_Visible
    reinterpret_cast<Method>(&CoreWindow_NoArgStub),           // 18 Activate
    reinterpret_cast<Method>(&CoreWindow_NoArgStub),           // 19 Close
    reinterpret_cast<Method>(&CoreWindow_GetKeyStateStub),     // 20 GetAsyncKeyState
    reinterpret_cast<Method>(&CoreWindow_GetKeyStateStub),     // 21 GetKeyState
    reinterpret_cast<Method>(&CoreWindow_NoArgStub),           // 22 ReleasePointerCapture
    reinterpret_cast<Method>(&CoreWindow_NoArgStub),           // 23 SetPointerCapture
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 24 add_Activated
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 25 remove_Activated
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 26 add_AutomationProviderRequested
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 27 remove_AutomationProviderRequested
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 28 add_CharacterReceived
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 29 remove_CharacterReceived
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 30 add_Closed
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 31 remove_Closed
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 32 add_InputEnabled
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 33 remove_InputEnabled
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 34 add_KeyDown
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 35 remove_KeyDown
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 36 add_KeyUp  <- el que reventaba
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 37 remove_KeyUp
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 38 add_PointerCaptureLost
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 39 remove_PointerCaptureLost
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 40 add_PointerEntered
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 41 remove_PointerEntered
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 42 add_PointerExited
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 43 remove_PointerExited
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 44 add_PointerMoved
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 45 remove_PointerMoved
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 46 add_PointerPressed
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 47 remove_PointerPressed
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 48 add_PointerReleased
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 49 remove_PointerReleased
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 50 add_TouchHitTesting
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 51 remove_TouchHitTesting
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 52 add_PointerWheelChanged
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 53 remove_PointerWheelChanged
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 54 add_SizeChanged
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 55 remove_SizeChanged
    reinterpret_cast<Method>(&CoreWindow_AddEventStub),        // 56 add_VisibilityChanged
    reinterpret_cast<Method>(&CoreWindow_RemoveEventStub),     // 57 remove_VisibilityChanged
};
static_assert(CountOf(g_core_window_vtable) == 58,
              "ICoreWindow tiene 58 slots; si falta alguno el juego despacha "
              "por indice fuera del array");

Method g_core_dispatcher_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&Dispatcher_get_HasThreadAccess),
    reinterpret_cast<Method>(&Dispatcher_ProcessEvents),
    reinterpret_cast<Method>(&Dispatcher_RunAsync),
    reinterpret_cast<Method>(&Dispatcher_RunIdleAsync),
};

// Los singletons se inicializan estaticamente. El original los rellena en
// tiempo de ejecucion desde Win32Bootstrap, pero el estado resultante es el
// mismo y asi estan listos antes de que corra nada.
Object g_core_window = {g_core_window_vtable, 1, kCoreWindowKind};
Object g_core_dispatcher = {g_core_dispatcher_vtable, 1, kCoreDispatcherKind};

}  // namespace

Object* CoreWindowSingleton() noexcept {
  return &g_core_window;
}

Object* CoreDispatcherSingleton() noexcept {
  return &g_core_dispatcher;
}

void Initialize() noexcept {
  g_queue_write = 0;
  g_queue_read = 0;
  g_queue_lock = 0;
  g_dispatch_depth = 0;
}

void ProcessQueuedHandlers(unsigned max_handlers) noexcept {
  if (max_handlers == 0) {
    return;
  }

  ++g_dispatch_depth;
  unsigned processed = 0;
  while (processed < max_handlers) {
    // El message loop original toma el mismo lock que QueueHandler para cada
    // pop y lo libera ANTES de invocar el delegado. Esto permite que el propio
    // callback (o un worker) vuelva a encolar sin deadlock y evita leer una
    // entrada a medio publicar.
    AcquireQueueLock();
    if (g_queue_read == g_queue_write) {
      ReleaseQueueLock();
      break;
    }

    const QueuedHandler entry = g_queue[g_queue_read];
    g_queue[g_queue_read].handler = nullptr;
    g_queue[g_queue_read].idle = false;
    g_queue_read = (g_queue_read + 1) & kQueueMask;
    ReleaseQueueLock();
    ++processed;

    void* handler = entry.handler;
    void* const* vtable = HandlerVtable(handler);
    if (vtable == nullptr) {
      continue;
    }

    // Invoke esta en el indice 3: los tres de IUnknown y luego el suyo. Los
    // delegados de WinRT derivan de IUnknown, no de IInspectable.
    //
    // LA ARIDAD NO ES LA MISMA EN LOS DOS. IDispatchedHandler::Invoke no lleva
    // parametros; IIdleDispatchedHandler::Invoke lleva uno. Bajo __stdcall la
    // pila la limpia el llamado, asi que llamar al primero con la firma del
    // segundo deja 4 bytes en la pila por cada manejador. El original usa
    // siempre la firma de dos argumentos y arrastra esa fuga; aqui se llama a
    // cada uno con la suya, que es la unica forma correcta.
    runtime_trace::Record(runtime_trace::BoundaryKind::Async,
                          "CoreDispatcher.Invoke.begin",
                          reinterpret_cast<uintptr_t>(handler),
                          entry.idle ? 1u : 0u);
    HRESULT result;
    if (entry.idle) {
      using IdleInvokeFn = HRESULT(SHIM_COM*)(void*, void*);
      result = reinterpret_cast<IdleInvokeFn>(vtable[kSlotInvoke])(handler,
                                                                   nullptr);
    } else {
      using InvokeFn = HRESULT(SHIM_COM*)(void*);
      result = reinterpret_cast<InvokeFn>(vtable[kSlotInvoke])(handler);
    }
    runtime_trace::Record(runtime_trace::BoundaryKind::Async,
                          "CoreDispatcher.Invoke.end",
                          reinterpret_cast<uintptr_t>(handler),
                          entry.idle ? 1u : 0u, result);

    log::Writef("CoreDispatcher.%s invoked handler=%p result=0x%08lX depth=%u",
                entry.idle ? "RunIdleAsync" : "RunAsync", handler, result,
                g_dispatch_depth);

    // Se suelta la referencia que se tomo al encolar. Va DESPUES de invocar: el
    // manejador puede ser lo unico que mantenga vivo su propio objeto.
    HandlerRelease(handler);
  }
  --g_dispatch_depth;
}

HRESULT SHIM_COM GetCoreWindowForCurrentThread(Object* factory,
                                               void** out) noexcept {
  (void)factory;
  const HRESULT result = ReturnSingleton(CoreWindowSingleton(), out);
  if (SUCCEEDED(result)) {
    log::Write("CoreWindow.GetForCurrentThread redirected to HWND window");
  }
  return result;
}

}  // namespace shim::winrt
