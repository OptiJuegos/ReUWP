#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reuwp {

// One parsed import thunk from the stock executable. Keeping this PE-neutral
// lets the compatibility policy be tested without constructing an Image.
struct ImportSymbolView {
  std::string name;
  uint32_t name_rva = 0;       // IMAGE_IMPORT_BY_NAME RVA, 0 for ordinal
  uint32_t int_entry_rva = 0;  // entry in the source descriptor's INT
  uint32_t iat_rva = 0;        // slot the loader ultimately writes
  uint16_t ordinal = 0;
  bool by_ordinal = false;
};

struct ImportModuleView {
  std::string name;
  size_t descriptor_index = 0;
  std::vector<ImportSymbolView> symbols;
};

enum class ImportRuleKind : uint8_t {
  NativeRedirect,
  Shim,
};

// Declarative compatibility rule. source_module/source_name identify the stock
// import; target_module is either a real Win7 DLL or "$shim".
struct ImportRule {
  const char* source_module;
  const char* source_name;
  ImportRuleKind kind;
  const char* target_module;
  const char* target_name;
};

// One loader binding synthesized into the .reuwp section. It points at the
// original IAT slot so game code keeps every stock address/offset.
struct ImportRedirect {
  size_t source_descriptor_index = 0;
  std::string source_module;
  std::string source_name;
  std::string target_module;
  std::string target_name;
  uint32_t int_entry_rva = 0;
  uint32_t iat_rva = 0;
  uint32_t neutralize_name_rva = 0;  // nonzero only when source descriptor stays
};

struct ImportPlan {
  std::vector<bool> keep_descriptor;
  std::vector<ImportRedirect> redirects;
};

// Builds a complete Win7 compatibility plan. API-set descriptors that ReUWP
// owns are dropped only when every symbol has an explicit rule. UCRT API sets
// are deliberately passed through unchanged.
bool BuildImportPlan(const std::vector<ImportModuleView>& modules,
                     const std::string& shim_dll, ImportPlan* plan,
                     std::string* error);

// Desktop VC runtime module rename used by the rebuilt import directory.
// Returns nullptr when the original module name should be preserved.
const char* DesktopRuntimeReplacement(const std::string& module) noexcept;

// Kept explicit so tests/tooling can assert that UCRT remains an external
// dependency rather than becoming part of ReUWP.
bool IsUcrtApiSet(const std::string& module) noexcept;

}  // namespace reuwp
