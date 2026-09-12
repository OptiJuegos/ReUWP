#include "app_model_internal.h"

#include "shim/runtime_trace.h"
#include "shim/store.h"

namespace shim::winrt {
namespace {

// --- ThreadPool -----------------------------------------------------------

// THREADPOOL_FIDELITY_EXACT
//
// Raw source of truth:
//   0x6294ED60 / 70 / 80  -- the three stdcall overload wrappers
//   0x6294ED90            -- shared core
//   0x6294EE90            -- worker thread procedure
//   0x629976D4..DC         -- {vtable=0x62993484, ref=1, kind=28}
//
// El original NO crea otro objeto para ThreadPool: reutiliza literalmente el
// singleton 0x629976D4, el mismo kind=28 que LoadListingInformationAsync.
// `store::ListingOperation()` conserva esa identidad compartida.

// El manejador que el juego entrega al ThreadPool, para pasarselo al hilo.
struct WorkItem {
  void* handler;
};

using HandlerRefFn = ULONG(SHIM_COM*)(void*);
using HandlerInvokeFn = HRESULT(SHIM_COM*)(void*, void*);
using CreateThreadFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, SIZE_T,
                                      LPTHREAD_START_ROUTINE, LPVOID, DWORD,
                                      LPDWORD);

CreateThreadFn g_create_thread = nullptr;

void* const* WorkItemVtable(void* handler) noexcept {
  if (handler == nullptr || ::IsBadReadPtr(handler, sizeof(void*))) {
    return nullptr;
  }
  void* const* vtable = *static_cast<void* const* const*>(handler);
  if (vtable == nullptr || ::IsBadReadPtr(vtable, 4 * sizeof(void*))) {
    return nullptr;
  }
  return vtable;
}

CreateThreadFn ResolveCreateThread() noexcept {
  if (g_create_thread != nullptr) {
    return g_create_thread;
  }
  const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
  if (kernel32 == nullptr) {
    return nullptr;
  }
  g_create_thread = reinterpret_cast<CreateThreadFn>(
      ::GetProcAddress(kernel32, "CreateThread"));
  return g_create_thread;
}

DWORD WINAPI RunWorkItem(LPVOID parameter) noexcept {
  auto* item = static_cast<WorkItem*>(parameter);
  void* handler = nullptr;
  void* const* vtable = nullptr;

  if (item != nullptr) {
    handler = item->handler;
    if (handler != nullptr) {
      vtable = WorkItemVtable(handler);
    }
    // El original libera el bloque de una palabra ANTES del Sleep/Invoke.
    ::HeapFree(::GetProcessHeap(), 0, item);
  }

  // 0x6294EEBE: Sleep(10), incluso si el parametro era nulo.
  ::Sleep(10);

  if (handler != nullptr && vtable != nullptr) {
    const auto invoke = reinterpret_cast<HandlerInvokeFn>(vtable[3]);
    const auto release = reinterpret_cast<HandlerRefFn>(vtable[2]);
    runtime_trace::Record(runtime_trace::BoundaryKind::Async,
                          "ThreadPool.Dispatch",
                          reinterpret_cast<uintptr_t>(handler),
                          reinterpret_cast<uintptr_t>(vtable),
                          static_cast<long>(reinterpret_cast<uintptr_t>(
                              store::ListingOperation())));
    if (invoke != nullptr) {
      runtime_trace::Record(runtime_trace::BoundaryKind::Async,
                            "ThreadPool.Invoke.begin",
                            reinterpret_cast<uintptr_t>(handler),
                            reinterpret_cast<uintptr_t>(invoke));
      const HRESULT invoke_result = invoke(handler, store::ListingOperation());
      runtime_trace::Record(runtime_trace::BoundaryKind::Async,
                            "ThreadPool.Invoke.end",
                            reinterpret_cast<uintptr_t>(handler),
                            reinterpret_cast<uintptr_t>(invoke),
                            invoke_result);
    }

    // Release se hace despues de Invoke aunque el slot Invoke fuera nulo.
    if (release != nullptr) {
      runtime_trace::Record(runtime_trace::BoundaryKind::Async,
                            "ThreadPool.Release.begin",
                            reinterpret_cast<uintptr_t>(handler),
                            reinterpret_cast<uintptr_t>(release),
                            static_cast<long>(reinterpret_cast<uintptr_t>(vtable)));
      const ULONG release_result = release(handler);
      runtime_trace::Record(runtime_trace::BoundaryKind::Async,
                            "ThreadPool.Release.end",
                            reinterpret_cast<uintptr_t>(handler),
                            reinterpret_cast<uintptr_t>(release),
                            static_cast<long>(release_result));
    }
  }
  return 0;
}

// Nucleo compartido equivalente a raw 0x6294ED90.
HRESULT RunHandlerOnThread(void* handler, void** operation) noexcept {
  void* const* vtable = WorkItemVtable(handler);
  if (vtable == nullptr) {
    return E_POINTER;
  }

  const auto add_ref = reinterpret_cast<HandlerRefFn>(vtable[1]);
  const auto release = reinterpret_cast<HandlerRefFn>(vtable[2]);
  if (add_ref == nullptr || release == nullptr) {
    return E_POINTER;
  }

  const CreateThreadFn create_thread = ResolveCreateThread();
  if (create_thread == nullptr) {
    return E_FAIL;
  }

  auto* item = static_cast<WorkItem*>(
      ::HeapAlloc(::GetProcessHeap(), 0, sizeof(WorkItem)));
  if (item == nullptr) {
    return E_OUTOFMEMORY;
  }
  item->handler = handler;

  // 0x6294EDE1: retain BEFORE publishing the pointer to the worker.
  add_ref(handler);

  const HANDLE thread =
      create_thread(nullptr, 0, &RunWorkItem, item, 0, nullptr);
  if (thread == nullptr) {
    // 0x6294EE54: failure path balances the retain and frees the work item.
    release(handler);
    ::HeapFree(::GetProcessHeap(), 0, item);
    return E_FAIL;
  }
  ::CloseHandle(thread);

  // The original still launches the worker when `operation` is null, then
  // returns E_POINTER.  Preserve that side effect/order.
  if (operation == nullptr) {
    return E_POINTER;
  }
  return ReturnSingleton(store::ListingOperation(), operation);
}

HRESULT SHIM_COM RunAsync(Object* self, void* handler,
                          void** operation) noexcept {
  (void)self;
  return RunHandlerOnThread(handler, operation);
}

// Las tres sobrecargas conservan las aridades raw: ret 0x0C/0x10/0x14.
HRESULT SHIM_COM RunAsyncWithPriority(Object* self, void* handler, int priority,
                                      void** operation) noexcept {
  (void)self;
  (void)priority;
  return RunHandlerOnThread(handler, operation);
}

HRESULT SHIM_COM RunAsyncWithPriorityAndOptions(Object* self, void* handler,
                                                int priority, int options,
                                                void** operation) noexcept {
  (void)self;
  (void)priority;
  (void)options;
  return RunHandlerOnThread(handler, operation);
}


#define SHIM_IINSPECTABLE                              \
  reinterpret_cast<Method>(&QueryInterface),           \
      reinterpret_cast<Method>(&AddRef),               \
      reinterpret_cast<Method>(&Release),              \
      reinterpret_cast<Method>(&GetIids),              \
      reinterpret_cast<Method>(&GetRuntimeClassName),  \
      reinterpret_cast<Method>(&GetTrustLevel)

// IThreadPoolStatics (0x62993D4C): tres sobrecargas de RunAsync.
Method g_thread_pool_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&RunAsync),
    reinterpret_cast<Method>(&RunAsyncWithPriority),
    reinterpret_cast<Method>(&RunAsyncWithPriorityAndOptions),
};

Object g_thread_pool = {g_thread_pool_vtable, 1, kXamlApplicationStatics};

}  // namespace

Object* ThreadPoolStatics() noexcept { return &g_thread_pool; }

#undef SHIM_IINSPECTABLE

}  // namespace shim::winrt
