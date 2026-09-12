#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "import_compat.h"
#include "patcher.h"
#include "pe.h"

namespace reuwp {

// Parsed view of the stock import directory. The PE descriptor bytes and the
// symbol-level policy view stay parallel by descriptor_index.
struct ImportView {
  std::vector<IMAGE_IMPORT_DESCRIPTOR> descriptors;
  std::vector<ImportModuleView> modules;
};

bool CollectImports(const pe::Image& image, ImportView* view,
                    std::string* error);

// Fixed offsets inside the new section. Both are needed before the section
// exists: the call site has to name the IAT slot, and the import directory has
// to name the descriptor array.
inline constexpr uint32_t kBootstrapIatOffset = 0x30;
inline constexpr uint32_t kDescriptorArrayOffset = 0x38;

uint32_t DescriptorArrayBytes(const ImportView& view,
                              const ImportPlan& plan) noexcept;

// Lays out the whole section: metadata block, bootstrap IAT, rebuilt descriptor
// array, redirect thunks/names and the journal.
bool BuildSectionContent(const ImportView& view, const ImportPlan& plan,
                         const Options& options, uint32_t section_rva,
                         uint32_t game_time_stamp, uint32_t game_image_size,
                         uint32_t bootstrap_rva,
                         const std::vector<Change>& changes,
                         std::vector<uint8_t>* content, std::string* error);

}  // namespace reuwp
