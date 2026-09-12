#include "section.h"

#include <cstring>

#include "shim/image_info.h"

namespace reuwp {
namespace {

constexpr const char* kBootstrapExport = "Win32Bootstrap";

bool ReadCString(const pe::Image& image, uint32_t rva, std::string* out) {
  const uint8_t* const start = image.At(rva, 1);
  if (start == nullptr) {
    return false;
  }
  const uint8_t* const limit = image.bytes().data() + image.bytes().size();
  const uint8_t* cursor = start;
  while (cursor < limit && *cursor != 0) {
    ++cursor;
  }
  if (cursor >= limit) {
    return false;
  }
  out->assign(reinterpret_cast<const char*>(start),
              static_cast<size_t>(cursor - start));
  return true;
}

// Appends into the section buffer and hands back the offset it landed at.
class Blob {
 public:
  explicit Blob(std::vector<uint8_t>* bytes) : bytes_(bytes) {}

  uint32_t Reserve(uint32_t size, uint32_t align) {
    Pad(align);
    const uint32_t at = static_cast<uint32_t>(bytes_->size());
    bytes_->resize(bytes_->size() + size, 0);
    return at;
  }

  uint32_t AppendBytes(const void* data, uint32_t size, uint32_t align) {
    const uint32_t at = Reserve(size, align);
    if (size != 0) {
      std::memcpy(bytes_->data() + at, data, size);
    }
    return at;
  }

  uint32_t AppendString(const std::string& text) {
    return AppendBytes(text.c_str(), static_cast<uint32_t>(text.size() + 1), 1);
  }

  // IMAGE_IMPORT_BY_NAME: a hint the loader is free to ignore, then the name.
  uint32_t AppendImportByName(const std::string& name) {
    const uint32_t at = Reserve(2, 2);
    bytes_->insert(bytes_->end(), name.begin(), name.end());
    bytes_->push_back(0);
    return at;
  }

 private:
  void Pad(uint32_t align) {
    while (align > 1 && (bytes_->size() % align) != 0) {
      bytes_->push_back(0);
    }
  }

  std::vector<uint8_t>* bytes_;
};

void StoreU32(std::vector<uint8_t>* bytes, uint32_t offset, uint32_t value) {
  std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

struct NamedRva {
  std::string name;
  uint32_t rva;
};

uint32_t InternModuleName(Blob* blob, uint32_t section_rva,
                          const std::string& name,
                          std::vector<NamedRva>* modules) {
  for (const NamedRva& module : *modules) {
    if (module.name == name) {
      return module.rva;
    }
  }
  const uint32_t rva = section_rva + blob->AppendString(name);
  modules->push_back({name, rva});
  return rva;
}

}  // namespace

uint32_t DescriptorArrayBytes(const ImportView& view,
                              const ImportPlan& plan) noexcept {
  size_t kept = 0;
  for (size_t i = 0; i < view.descriptors.size() &&
                     i < plan.keep_descriptor.size();
       ++i) {
    if (plan.keep_descriptor[i]) {
      ++kept;
    }
  }
  // Kept originals + one descriptor per redirected IAT slot + bootstrap + null.
  const size_t count = kept + plan.redirects.size() + 2;
  return static_cast<uint32_t>(count * sizeof(IMAGE_IMPORT_DESCRIPTOR));
}

bool CollectImports(const pe::Image& image, ImportView* view,
                    std::string* error) {
  if (view == nullptr || error == nullptr) {
    return false;
  }
  view->descriptors.clear();
  view->modules.clear();

  const IMAGE_DATA_DIRECTORY& dir =
      image.nt()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (dir.VirtualAddress == 0 || dir.Size == 0) {
    *error = "image has no import directory";
    return false;
  }

  uint32_t rva = dir.VirtualAddress;
  for (;;) {
    const auto* const descriptor =
        reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
            image.At(rva, sizeof(IMAGE_IMPORT_DESCRIPTOR)));
    if (descriptor == nullptr) {
      *error = "import directory runs outside the file";
      return false;
    }
    if (descriptor->Name == 0 && descriptor->FirstThunk == 0) {
      break;
    }

    std::string module_name;
    if (!ReadCString(image, descriptor->Name, &module_name)) {
      *error = "unreadable import module name";
      return false;
    }

    ImportModuleView module;
    module.name = module_name;
    module.descriptor_index = view->descriptors.size();

    const uint32_t table = descriptor->OriginalFirstThunk != 0
                               ? descriptor->OriginalFirstThunk
                               : descriptor->FirstThunk;
    for (uint32_t index = 0;; ++index) {
      const uint32_t entry_rva = table + index * sizeof(uint32_t);
      const auto* const thunk = reinterpret_cast<const uint32_t*>(
          image.At(entry_rva, sizeof(uint32_t)));
      if (thunk == nullptr) {
        *error = "import name table runs outside the file: " + module_name;
        return false;
      }
      if (*thunk == 0) {
        break;
      }

      ImportSymbolView symbol;
      symbol.int_entry_rva = entry_rva;
      symbol.iat_rva = descriptor->FirstThunk + index * sizeof(uint32_t);
      if ((*thunk & IMAGE_ORDINAL_FLAG32) != 0) {
        symbol.by_ordinal = true;
        symbol.ordinal = static_cast<uint16_t>(*thunk & 0xFFFFu);
      } else {
        symbol.name_rva = *thunk;
        if (!ReadCString(image, *thunk + 2, &symbol.name)) {
          *error = "unreadable import symbol in module: " + module_name;
          return false;
        }
      }
      module.symbols.push_back(std::move(symbol));
    }

    view->descriptors.push_back(*descriptor);
    view->modules.push_back(std::move(module));
    rva += sizeof(IMAGE_IMPORT_DESCRIPTOR);
  }
  return true;
}

bool BuildSectionContent(const ImportView& view, const ImportPlan& plan,
                         const Options& options, uint32_t section_rva,
                         uint32_t game_time_stamp, uint32_t game_image_size,
                         uint32_t bootstrap_rva,
                         const std::vector<Change>& changes,
                         std::vector<uint8_t>* content, std::string* error) {
  if (plan.keep_descriptor.size() != view.descriptors.size()) {
    *error = "import plan does not match descriptor count";
    return false;
  }

  content->clear();
  Blob blob(content);

  // Fixed prefix. Both offsets were committed to before the section existed:
  // the call site names the IAT slot and the import directory names the array.
  blob.Reserve(kBootstrapIatOffset, 1);
  const uint32_t iat_offset = blob.Reserve(8, 4);
  const uint32_t descriptors_offset =
      blob.Reserve(DescriptorArrayBytes(view, plan), 4);
  if (iat_offset != kBootstrapIatOffset ||
      descriptors_offset != kDescriptorArrayOffset) {
    *error = "section prefix does not match the published offsets";
    return false;
  }

  std::vector<NamedRva> module_names;
  const uint32_t shim_name_rva =
      InternModuleName(&blob, section_rva, options.shim_dll, &module_names);
  const uint32_t bootstrap_name_rva =
      section_rva + blob.AppendImportByName(kBootstrapExport);

  // Every redirect gets a tiny private INT. FirstThunk deliberately points at
  // the stock IAT slot, so the loader resolves into the address the game has
  // always used even when one source API-set splits across several host DLLs.
  std::vector<uint32_t> redirect_tables;
  std::vector<uint32_t> redirect_module_rvas;
  redirect_tables.reserve(plan.redirects.size());
  redirect_module_rvas.reserve(plan.redirects.size());
  for (const ImportRedirect& redirect : plan.redirects) {
    const uint32_t import_name_rva =
        section_rva + blob.AppendImportByName(redirect.target_name);
    const uint32_t table = blob.Reserve(8, 4);
    StoreU32(content, table, import_name_rva);
    redirect_tables.push_back(section_rva + table);
    redirect_module_rvas.push_back(InternModuleName(
        &blob, section_rva, redirect.target_module, &module_names));
  }

  const uint32_t bootstrap_table = blob.Reserve(8, 4);
  StoreU32(content, bootstrap_table, bootstrap_name_rva);

  // Fresh strings only for descriptors whose module name changes from the UWP
  // VC runtime to its desktop counterpart.
  std::vector<uint32_t> renamed(view.descriptors.size(), 0);
  for (size_t i = 0; i < view.modules.size(); ++i) {
    const char* const replacement = DesktopRuntimeReplacement(view.modules[i].name);
    if (replacement != nullptr) {
      renamed[i] = InternModuleName(&blob, section_rva, replacement,
                                    &module_names);
    }
  }

  // Journal of stock bytes, so a patched image can be taken back.
  std::vector<shim::game::ImageInfoJournalEntry> journal;
  journal.reserve(changes.size());
  for (const Change& change : changes) {
    const uint32_t at = blob.AppendBytes(
        change.original.data(), static_cast<uint32_t>(change.original.size()),
        1);
    journal.push_back({change.rva,
                       static_cast<uint32_t>(change.original.size()),
                       section_rva + at, 0});
  }
  uint32_t journal_rva = 0;
  if (!journal.empty()) {
    const uint32_t at = blob.AppendBytes(
        journal.data(),
        static_cast<uint32_t>(journal.size() * sizeof(journal[0])), 4);
    journal_rva = section_rva + at;
  }

  // The bootstrap IAT starts out naming the export; the loader replaces it with
  // the resolved address before the call site runs.
  StoreU32(content, iat_offset, bootstrap_name_rva);

  auto* const out = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      content->data() + descriptors_offset);
  size_t next = 0;
  for (size_t i = 0; i < view.descriptors.size(); ++i) {
    if (!plan.keep_descriptor[i]) {
      continue;
    }
    out[next] = view.descriptors[i];
    if (renamed[i] != 0) {
      out[next].Name = renamed[i];
    }
    ++next;
  }

  for (size_t i = 0; i < plan.redirects.size(); ++i, ++next) {
    std::memset(&out[next], 0, sizeof(out[next]));
    out[next].OriginalFirstThunk = redirect_tables[i];
    out[next].Name = redirect_module_rvas[i];
    out[next].FirstThunk = plan.redirects[i].iat_rva;
  }

  std::memset(&out[next], 0, sizeof(out[next]));
  out[next].OriginalFirstThunk = section_rva + bootstrap_table;
  out[next].Name = shim_name_rva;
  out[next].FirstThunk = section_rva + kBootstrapIatOffset;
  ++next;
  std::memset(&out[next], 0, sizeof(out[next]));

  shim::game::ImageInfo info = {};
  std::memcpy(info.magic, shim::game::kImageInfoMagic, sizeof(info.magic));
  info.format = shim::game::kImageInfoFormat;
  info.header_size = sizeof(info);
  info.game_time_stamp = game_time_stamp;
  info.game_image_size = game_image_size;
  info.patcher_version = options.patcher_version;
  info.bootstrap_rva = bootstrap_rva;
  info.journal_rva = journal_rva;
  info.journal_count = static_cast<uint32_t>(journal.size());
  info.checksum = shim::game::ImageInfoChecksum(info);
  std::memcpy(content->data(), &info, sizeof(info));
  return true;
}

}  // namespace reuwp
