#include "import_compat.h"

#include <cctype>

namespace reuwp {
namespace {

constexpr const char* kShimTarget = "$shim";

// The table is intentionally symbol-level. API-set contracts are not real DLL
// boundaries on Win7 and a single source descriptor can split across multiple
// host DLLs (COM is the obvious example).
constexpr ImportRule kRules[] = {
    // KERNEL32 imports introduced after Windows 7.
    {"kernel32.dll", "CreateFile2", ImportRuleKind::Shim, kShimTarget,
     "CreateFile2"},
    {"kernel32.dll", "ResolveDelayLoadedAPI", ImportRuleKind::Shim,
     kShimTarget, "ResolveDelayLoadedAPI"},
    {"kernel32.dll", "ResolveDelayLoadsFromDll", ImportRuleKind::Shim,
     kShimTarget, "ResolveDelayLoadsFromDll"},
    {"kernel32.dll", "DelayLoadFailureHook", ImportRuleKind::Shim,
     kShimTarget, "DelayLoadFailureHook"},

    // XINPUTUAP is a UWP-only facade. Win7 ships the legacy 9.1.0 XInput
    // implementation in-box and XInputGetState has the same ABI.
    {"XINPUTUAP.dll", "XInputGetState", ImportRuleKind::NativeRedirect,
     "xinput9_1_0.dll", "XInputGetState"},

    // api-ms-win-core-com-l1-1-1.dll. Everything except the AppContainer-only
    // activation helper exists in OLE32 on the Win7 baseline.
    {"api-ms-win-core-com-l1-1-1.dll", "StringFromGUID2",
     ImportRuleKind::NativeRedirect, "ole32.dll", "StringFromGUID2"},
    {"api-ms-win-core-com-l1-1-1.dll", "CoTaskMemAlloc",
     ImportRuleKind::NativeRedirect, "ole32.dll", "CoTaskMemAlloc"},
    {"api-ms-win-core-com-l1-1-1.dll", "CoTaskMemFree",
     ImportRuleKind::NativeRedirect, "ole32.dll", "CoTaskMemFree"},
    {"api-ms-win-core-com-l1-1-1.dll", "CoCreateInstanceFromApp",
     ImportRuleKind::Shim, kShimTarget, "CoCreateInstanceFromApp"},
    {"api-ms-win-core-com-l1-1-1.dll", "CoCreateGuid",
     ImportRuleKind::NativeRedirect, "ole32.dll", "CoCreateGuid"},
    {"api-ms-win-core-com-l1-1-1.dll", "CoGetContextToken",
     ImportRuleKind::NativeRedirect, "ole32.dll", "CoGetContextToken"},
    {"api-ms-win-core-com-l1-1-1.dll", "IIDFromString",
     ImportRuleKind::NativeRedirect, "ole32.dll", "IIDFromString"},
    {"api-ms-win-core-com-l1-1-1.dll", "CoCreateFreeThreadedMarshaler",
     ImportRuleKind::NativeRedirect, "ole32.dll",
     "CoCreateFreeThreadedMarshaler"},

    // ETW provider API: three calls are present on Vista/Win7. EventSetInformation
    // is Win8+, so ReUWP supplies the graceful-not-supported compatibility call.
    {"api-ms-win-eventing-provider-l1-1-0.dll", "EventRegister",
     ImportRuleKind::NativeRedirect, "advapi32.dll", "EventRegister"},
    {"api-ms-win-eventing-provider-l1-1-0.dll", "EventUnregister",
     ImportRuleKind::NativeRedirect, "advapi32.dll", "EventUnregister"},
    {"api-ms-win-eventing-provider-l1-1-0.dll", "EventWriteTransfer",
     ImportRuleKind::NativeRedirect, "advapi32.dll", "EventWriteTransfer"},
    {"api-ms-win-eventing-provider-l1-1-0.dll", "EventSetInformation",
     ImportRuleKind::Shim, kShimTarget, "EventSetInformation"},

    // UWP/Xbox title-callable UI has no Win7 equivalent.
    {"api-ms-win-gaming-tcui-l1-1-2.dll", "ProcessPendingGameUI",
     ImportRuleKind::Shim, kShimTarget, "ProcessPendingGameUI"},
    {"api-ms-win-gaming-tcui-l1-1-2.dll", "ShowProfileCardUI",
     ImportRuleKind::Shim, kShimTarget, "ShowProfileCardUI"},

    // Older Bedrock builds use the l1-1-0 contract name for the same ABI.
    {"api-ms-win-gaming-tcui-l1-1-0.dll", "ProcessPendingGameUI",
     ImportRuleKind::Shim, kShimTarget, "ProcessPendingGameUI"},
    {"api-ms-win-gaming-tcui-l1-1-0.dll", "ShowProfileCardUI",
     ImportRuleKind::Shim, kShimTarget, "ShowProfileCardUI"},

    // Restricted-error surface. ReUWP owns the Win7 behavior.
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "RoFailFastWithErrorContext",
     ImportRuleKind::Shim, kShimTarget, "RoFailFastWithErrorContext"},
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "RoReportUnhandledError",
     ImportRuleKind::Shim, kShimTarget, "RoReportUnhandledError"},
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "SetRestrictedErrorInfo",
     ImportRuleKind::Shim, kShimTarget, "SetRestrictedErrorInfo"},
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "GetRestrictedErrorInfo",
     ImportRuleKind::Shim, kShimTarget, "GetRestrictedErrorInfo"},
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "RoCaptureErrorContext",
     ImportRuleKind::Shim, kShimTarget, "RoCaptureErrorContext"},
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "RoOriginateError",
     ImportRuleKind::Shim, kShimTarget, "RoOriginateError"},
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "RoOriginateLanguageException",
     ImportRuleKind::Shim, kShimTarget, "RoOriginateLanguageException"},
    {"api-ms-win-core-winrt-error-l1-1-1.dll", "RoTransformError",
     ImportRuleKind::Shim, kShimTarget, "RoTransformError"},

    // Self-contained HSTRING ABI. Keeping these all in ReUWP also lets the shim
    // itself stop linking runtimeobject.lib.
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsConcatString",
     ImportRuleKind::Shim, kShimTarget, "WindowsConcatString"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsDuplicateString",
     ImportRuleKind::Shim, kShimTarget, "WindowsDuplicateString"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsCreateString",
     ImportRuleKind::Shim, kShimTarget, "WindowsCreateString"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsCreateStringReference",
     ImportRuleKind::Shim, kShimTarget, "WindowsCreateStringReference"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsDeleteString",
     ImportRuleKind::Shim, kShimTarget, "WindowsDeleteString"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsCompareStringOrdinal",
     ImportRuleKind::Shim, kShimTarget, "WindowsCompareStringOrdinal"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsIsStringEmpty",
     ImportRuleKind::Shim, kShimTarget, "WindowsIsStringEmpty"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsGetStringRawBuffer",
     ImportRuleKind::Shim, kShimTarget, "WindowsGetStringRawBuffer"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsGetStringLen",
     ImportRuleKind::Shim, kShimTarget, "WindowsGetStringLen"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsPreallocateStringBuffer",
     ImportRuleKind::Shim, kShimTarget, "WindowsPreallocateStringBuffer"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsPromoteStringBuffer",
     ImportRuleKind::Shim, kShimTarget, "WindowsPromoteStringBuffer"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsDeleteStringBuffer",
     ImportRuleKind::Shim, kShimTarget, "WindowsDeleteStringBuffer"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsStringHasEmbeddedNull",
     ImportRuleKind::Shim, kShimTarget, "WindowsStringHasEmbeddedNull"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsSubstring",
     ImportRuleKind::Shim, kShimTarget, "WindowsSubstring"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll",
     "WindowsSubstringWithSpecifiedLength", ImportRuleKind::Shim, kShimTarget,
     "WindowsSubstringWithSpecifiedLength"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsReplaceString",
     ImportRuleKind::Shim, kShimTarget, "WindowsReplaceString"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsTrimStringStart",
     ImportRuleKind::Shim, kShimTarget, "WindowsTrimStringStart"},
    {"api-ms-win-core-winrt-string-l1-1-0.dll", "WindowsTrimStringEnd",
     ImportRuleKind::Shim, kShimTarget, "WindowsTrimStringEnd"},

    {"api-ms-win-core-winrt-l1-1-0.dll", "RoInitialize",
     ImportRuleKind::Shim, kShimTarget, "RoInitialize"},
    {"api-ms-win-core-winrt-l1-1-0.dll", "RoUninitialize",
     ImportRuleKind::Shim, kShimTarget, "RoUninitialize"},
    {"api-ms-win-core-winrt-l1-1-0.dll", "RoGetActivationFactory",
     ImportRuleKind::Shim, kShimTarget, "RoGetActivationFactory"},
    {"api-ms-win-core-winrt-l1-1-0.dll", "RoActivateInstance",
     ImportRuleKind::Shim, kShimTarget, "RoActivateInstance"},
    {"api-ms-win-core-winrt-l1-1-0.dll", "RoGetApartmentIdentifier",
     ImportRuleKind::Shim, kShimTarget, "RoGetApartmentIdentifier"},
    {"api-ms-win-core-winrt-l1-1-0.dll", "RoRegisterForApartmentShutdown",
     ImportRuleKind::Shim, kShimTarget, "RoRegisterForApartmentShutdown"},
    {"api-ms-win-core-winrt-l1-1-0.dll", "RoUnregisterForApartmentShutdown",
     ImportRuleKind::Shim, kShimTarget, "RoUnregisterForApartmentShutdown"},
};

struct ModuleRename {
  const char* from;
  const char* to;
};

constexpr ModuleRename kDesktopRuntimeRenames[] = {
    {"vccorlib140_app.DLL", "vccorlib140.DLL"},
    {"MSVCP140_APP.dll", "MSVCP140.dll"},
    {"CONCRT140_APP.dll", "CONCRT140.dll"},
    {"VCRUNTIME140_APP.dll", "VCRUNTIME140.dll"},
};

bool EqualsNoCase(const std::string& text, const char* other) noexcept {
  size_t i = 0;
  for (; i < text.size() && other[i] != 0; ++i) {
    if (::tolower(static_cast<unsigned char>(text[i])) !=
        ::tolower(static_cast<unsigned char>(other[i]))) {
      return false;
    }
  }
  return i == text.size() && other[i] == 0;
}

bool StartsWithNoCase(const std::string& text, const char* prefix) noexcept {
  size_t i = 0;
  for (; prefix[i] != 0; ++i) {
    if (i >= text.size() ||
        ::tolower(static_cast<unsigned char>(text[i])) !=
            ::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

const ImportRule* FindRule(const std::string& module,
                           const std::string& name) noexcept {
  const char* lookup_module = module.c_str();
  if (EqualsNoCase(module, "api-ms-win-core-winrt-string-l1-1-1.dll")) {
    lookup_module = "api-ms-win-core-winrt-string-l1-1-0.dll";
  }
  for (const ImportRule& rule : kRules) {
    if (EqualsNoCase(lookup_module, rule.source_module) &&
        name == rule.source_name) {
      return &rule;
    }
  }
  return nullptr;
}

bool IsFullyOwnedApiSet(const std::string& module) noexcept {
  return EqualsNoCase(module, "XINPUTUAP.dll") ||
         EqualsNoCase(module, "api-ms-win-core-com-l1-1-1.dll") ||
         EqualsNoCase(module, "api-ms-win-eventing-provider-l1-1-0.dll") ||
         EqualsNoCase(module, "api-ms-win-gaming-tcui-l1-1-2.dll") ||
         EqualsNoCase(module, "api-ms-win-gaming-tcui-l1-1-0.dll") ||
         EqualsNoCase(module, "api-ms-win-core-winrt-error-l1-1-1.dll") ||
         EqualsNoCase(module, "api-ms-win-core-winrt-string-l1-1-0.dll") ||
         EqualsNoCase(module, "api-ms-win-core-winrt-string-l1-1-1.dll") ||
         EqualsNoCase(module, "api-ms-win-core-winrt-l1-1-0.dll");
}

const ImportSymbolView* FindSymbol(const ImportModuleView& module,
                                   const char* name) noexcept {
  for (const ImportSymbolView& symbol : module.symbols) {
    if (!symbol.by_ordinal && symbol.name == name) {
      return &symbol;
    }
  }
  return nullptr;
}

}  // namespace

bool IsUcrtApiSet(const std::string& module) noexcept {
  // UCRT is serviced separately on Windows 7. ReUWP is not a C runtime.
  return StartsWithNoCase(module, "api-ms-win-crt-");
}

const char* DesktopRuntimeReplacement(const std::string& module) noexcept {
  for (const ModuleRename& rename : kDesktopRuntimeRenames) {
    if (EqualsNoCase(module, rename.from)) {
      return rename.to;
    }
  }
  return nullptr;
}

bool BuildImportPlan(const std::vector<ImportModuleView>& modules,
                     const std::string& shim_dll, ImportPlan* plan,
                     std::string* error) {
  if (plan == nullptr || error == nullptr) {
    return false;
  }
  plan->keep_descriptor.assign(modules.size(), true);
  plan->redirects.clear();

  for (const ImportModuleView& module : modules) {
    const bool fully_owned = IsFullyOwnedApiSet(module.name);
    if (fully_owned) {
      plan->keep_descriptor[module.descriptor_index] = false;
    }

    // KERNEL32 stays loaded, so redirected slots first need a harmless Win7
    // name in its original INT. The later ReUWP descriptor overwrites the same
    // IAT slot with the compatibility export.
    uint32_t kernel32_substitute = 0;
    if (EqualsNoCase(module.name, "kernel32.dll")) {
      const ImportSymbolView* substitute = FindSymbol(module, "GetLastError");
      if (substitute != nullptr) {
        kernel32_substitute = substitute->name_rva;
      }
    }

    for (const ImportSymbolView& symbol : module.symbols) {
      if (symbol.by_ordinal) {
        if (fully_owned) {
          *error = "unsupported ordinal import in Win7 compatibility API-set: " +
                   module.name;
          return false;
        }
        continue;
      }

      const ImportRule* const rule = FindRule(module.name, symbol.name);
      if (rule == nullptr) {
        if (fully_owned) {
          *error = "unmapped Win7 compatibility import: " + module.name + "!" +
                   symbol.name;
          return false;
        }
        continue;
      }

      ImportRedirect redirect;
      redirect.source_descriptor_index = module.descriptor_index;
      redirect.source_module = module.name;
      redirect.source_name = symbol.name;
      redirect.target_module =
          rule->kind == ImportRuleKind::Shim ? shim_dll : rule->target_module;
      redirect.target_name = rule->target_name;
      redirect.int_entry_rva = symbol.int_entry_rva;
      redirect.iat_rva = symbol.iat_rva;
      if (!fully_owned) {
        if (kernel32_substitute == 0) {
          *error = "retained import descriptor has no neutralizer: " +
                   module.name;
          return false;
        }
        redirect.neutralize_name_rva = kernel32_substitute;
      }
      plan->redirects.push_back(std::move(redirect));
    }

    if (fully_owned) {
      // Complete coverage is mandatory before removing a source descriptor.
      size_t covered = 0;
      for (const ImportRedirect& redirect : plan->redirects) {
        if (redirect.source_descriptor_index == module.descriptor_index) {
          ++covered;
        }
      }
      if (covered != module.symbols.size()) {
        *error = "incomplete Win7 API-set coverage: " + module.name;
        return false;
      }
    }
  }

  // Explicit policy: UCRT API sets are not rewritten by this compatibility
  // layer. They remain original descriptors and are supplied externally.
  for (const ImportModuleView& module : modules) {
    if (IsUcrtApiSet(module.name) &&
        !plan->keep_descriptor[module.descriptor_index]) {
      *error = "internal error: api-ms-win-crt-* must remain pass-through";
      return false;
    }
  }
  return true;
}

}  // namespace reuwp
