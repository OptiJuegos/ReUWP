#include "app_model_internal.h"

#include "shim/app_window.h"
#include "shim/log.h"

namespace shim::winrt {
namespace {


#define SHIM_IINSPECTABLE                              \
  reinterpret_cast<Method>(&QueryInterface),           \
      reinterpret_cast<Method>(&AddRef),               \
      reinterpret_cast<Method>(&Release),              \
      reinterpret_cast<Method>(&GetIids),              \
      reinterpret_cast<Method>(&GetRuntimeClassName),  \
      reinterpret_cast<Method>(&GetTrustLevel)

// XAML: el juego pregunta por Application.Current y Window.Current durante el
// arranque. En 1.1.5, Window.Content no puede ser un objeto totalmente inerte:
// AppMainXaml lo usa como size source y consulta su rasterization scale.
HRESULT SHIM_COM GetXamlWindowContent(Object* self, void** out) noexcept;
HRESULT SHIM_COM GetXamlRasterizationScale(Object* self, float* out) noexcept;
HRESULT SHIM_COM GetXamlActualSize(Object* self, float* out) noexcept;

// dword_62998EF8 en el DLL. Solo 1.1.5 necesita que el objeto XAML lea el
// AppPlatform para convertir pixeles fisicos a unidades logicas.
void* g_xaml_app_main = nullptr;

// 0x6294C7E0 is deliberately a one-argument __stdcall no-op (`ret 4`).
// Reusing a two-argument E_NOTIMPL getter here would pop four bytes too many.
HRESULT SHIM_COM XamlApplicationNoOp(Object* self) noexcept {
  (void)self;
  return S_OK;
}

// 0x629935A4: 18 slots; slots 6..17 all point to the ret-4 no-op above.
Method g_xaml_application_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
    reinterpret_cast<Method>(&XamlApplicationNoOp),
};
static_assert(CountOf(g_xaml_application_vtable) == 18,
              "XAML Application must match 0x629935A4");

// off_62993608 tiene 11 entradas: los cuatro slots 6..9 son NULL en el
// binario original y get_Content vive en el slot 10. Se mantienen NULL de forma
// literal: inventar una firma __stdcall aqui cambiaria la limpieza de ESP.
Method g_xaml_window_vtable[] = {
    SHIM_IINSPECTABLE,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<Method>(&GetXamlWindowContent),
};

// off_62993634. Esta tabla es mucho mas larga de lo que parece al mirar solo
// el primer getter: get_RasterizationScale vive en el slot 6, hay 16 huecos
// NULL exactamente como en el DLL y UIElement.ActualSize aparece en el slot
// 23 (0x6294C980). Acortar la tabla a siete entradas hace que AppMainXaml
// termine llamando memoria adyacente durante resize.
Method g_xaml_size_source_vtable[] = {
    SHIM_IINSPECTABLE,                                      //  0..5
    reinterpret_cast<Method>(&GetXamlRasterizationScale),  //  6
    nullptr,  //  7
    nullptr,  //  8
    nullptr,  //  9
    nullptr,  // 10
    nullptr,  // 11
    nullptr,  // 12
    nullptr,  // 13
    nullptr,  // 14
    nullptr,  // 15
    nullptr,  // 16
    nullptr,  // 17
    nullptr,  // 18
    nullptr,  // 19
    nullptr,  // 20
    nullptr,  // 21
    nullptr,  // 22
    reinterpret_cast<Method>(&GetXamlActualSize),           // 23
};

Object g_xaml_application = {g_xaml_application_vtable, 1, kXamlApplication};
Object g_xaml_window = {g_xaml_window_vtable, 1, kXamlWindow};
Object g_xaml_size_source = {g_xaml_size_source_vtable, 1, kXamlWindow};

HRESULT SHIM_COM GetXamlWindowContent(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_xaml_size_source, out);
  if (SUCCEEDED(result)) {
    log::Write("XAML Window.Content redirected to HWND size source");
  }
  return result;
}

HRESULT SHIM_COM GetXamlRasterizationScale(Object* self, float* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1.0f;
  log::Write("XAML size-source rasterization scale returned 1.0");
  return S_OK;
}

// 0x6294C980 (IDA lo etiqueta 0x6294C960): UIElement.ActualSize. El juego
// espera dos float consecutivos. La ventana Win32 entrega pixeles fisicos; el
// original divide por las escalas guardadas en AppPlatform+0x204/+0x208, pero
// solo si ambas estan en el rango sano (0.0001, 100).
HRESULT SHIM_COM GetXamlActualSize(Object* self, float* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }

  float scale_x = 1.0f;
  float scale_y = 1.0f;
  if (g_xaml_app_main != nullptr &&
      !::IsBadReadPtr(g_xaml_app_main, 8)) {
    void* const platform = *reinterpret_cast<void**>(
        static_cast<unsigned char*>(g_xaml_app_main) + 0x04);
    if (platform != nullptr &&
        !::IsBadReadPtr(static_cast<unsigned char*>(platform) + 0x204, 8) &&
        !::IsBadReadPtr(static_cast<unsigned char*>(platform) + 0x20C, 1)) {
      const float candidate_x = *reinterpret_cast<const float*>(
          static_cast<unsigned char*>(platform) + 0x204);
      const float candidate_y = *reinterpret_cast<const float*>(
          static_cast<unsigned char*>(platform) + 0x208);
      constexpr float kMinScale = 0.0001f;
      constexpr float kMaxScale = 100.0f;
      if (candidate_x > kMinScale && candidate_x < kMaxScale &&
          candidate_y > kMinScale && candidate_y < kMaxScale) {
        scale_x = candidate_x;
        scale_y = candidate_y;
      }
    }
  }

  RECT client{};
  const HWND window = app_window::Handle();
  if (window == nullptr || !::GetClientRect(app_window::Handle(), &client)) {
    out[0] = 1280.0f;
    out[1] = 720.0f;
    log::Write("XAML UIElement.ActualSize used 1280x720 fallback");
    return S_OK;
  }

  out[0] = static_cast<float>(client.right - client.left) / scale_x;
  out[1] = static_cast<float>(client.bottom - client.top) / scale_y;
  return S_OK;
}

HRESULT SHIM_COM GetCurrentXamlApplication(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_xaml_application, out);
  if (SUCCEEDED(result)) {
    log::Write("XAML Application.Current redirected to HWND host");
  }
  return result;
}

HRESULT SHIM_COM GetCurrentXamlWindow(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_xaml_window, out);
  if (SUCCEEDED(result)) {
    log::Write("XAML Window.Current redirected to HWND window");
  }
  return result;
}

Method g_xaml_application_statics_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetCurrentXamlApplication),
};

Method g_xaml_window_statics_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetCurrentXamlWindow),
};

Object g_xaml_application_statics = {g_xaml_application_statics_vtable, 1,
                                     kXamlApplicationStatics};
Object g_xaml_window_statics = {g_xaml_window_statics_vtable, 1,
                                kXamlWindowStatics};

}  // namespace

Object* XamlApplicationStatics() noexcept { return &g_xaml_application_statics; }
Object* XamlWindowStatics() noexcept { return &g_xaml_window_statics; }
Object* XamlSizeSourceSingleton() noexcept { return &g_xaml_size_source; }

void SetXamlAppMain(void* app_main) noexcept { g_xaml_app_main = app_main; }

#undef SHIM_IINSPECTABLE

}  // namespace shim::winrt
