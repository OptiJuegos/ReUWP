#include "patcher.h"

#include <cstring>

#include "section.h"

namespace reuwp {
namespace {

// Builds this patcher knows. Identity is the stock TimeDateStamp; SizeOfImage
// is only a cross-check, because it moves with the patch geometry.
constexpr Target kTargets[] = {
    {"0.15.10", 0x57EEFE46, 0x00D2E000, 0x0087CD54},
    {"1.1.5", 0x5976D9CF, 0x01586000, 0x00E20563},
};

// The CRT hands control to the application here: push the show command, the
// command line and the module handle, then call. Patching this exact call is
// what puts the shim in front of the UWP application model, and it has to be
// this call rather than the entry point, because the game's static
// constructors have to have run by then.
constexpr uint8_t kBootstrapCallSize = 12;

constexpr uint16_t kWindows7Version = 0x0601;  // major 6, minor 1

// Loaded dynamically by the game rather than imported, so the string cannot be
// repointed: the code holds its address. It is shortened where it stands.
constexpr char kFmodUwpName[] = "fmod_WSA82_X86.dll";
constexpr char kFmodName[] = "fmod.dll";

void Record(std::vector<Change>* changes, uint32_t rva, size_t offset,
            const uint8_t* before, const uint8_t* after, size_t size,
            std::string what) {
  Change change;
  change.rva = rva;
  change.file_offset = static_cast<uint32_t>(offset);
  change.original.assign(before, before + size);
  change.patched.assign(after, after + size);
  change.what = std::move(what);
  changes->push_back(std::move(change));
}

// Writes over `size` bytes at `rva` and records what was there.
bool WriteAt(pe::Image& image, uint32_t rva, const void* data, size_t size,
             const char* what, std::vector<Change>* changes,
             std::string* error) {
  uint8_t* const target = image.At(rva, size);
  if (target == nullptr) {
    *error = std::string(what) + ": rva is not backed by file bytes";
    return false;
  }
  size_t offset = 0;
  image.RvaToOffset(rva, &offset);
  Record(changes, rva, offset, target,
         static_cast<const uint8_t*>(data), size, what);
  std::memcpy(target, data, size);
  return true;
}

// The PE headers are mapped at RVA == file offset, so header edits go through
// the same journal as everything else.
bool WriteHeader(pe::Image& image, size_t offset, const void* data, size_t size,
                 const char* what, std::vector<Change>* changes,
                 std::string* error) {
  if (offset + size > image.bytes().size()) {
    *error = std::string(what) + ": header write runs past the file";
    return false;
  }
  uint8_t* const target = image.bytes().data() + offset;
  Record(changes, static_cast<uint32_t>(offset), offset, target,
         static_cast<const uint8_t*>(data), size, what);
  std::memcpy(target, data, size);
  return true;
}

bool RelaxHeader(pe::Image& image, const Options& options,
                 std::vector<Change>* changes, std::string* error) {
  IMAGE_OPTIONAL_HEADER32& optional = image.nt()->OptionalHeader;
  const uint8_t* const base = image.bytes().data();

  uint16_t characteristics = optional.DllCharacteristics;
  // APPCONTAINER is the flag that makes the loader refuse to run the image
  // outside a UWP package. Nothing else here matters as much.
  characteristics &= static_cast<uint16_t>(~IMAGE_DLLCHARACTERISTICS_APPCONTAINER);
  if (!options.keep_aslr) {
    characteristics &=
        static_cast<uint16_t>(~IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE);
  }
  if (characteristics != optional.DllCharacteristics) {
    const size_t offset =
        reinterpret_cast<const uint8_t*>(&optional.DllCharacteristics) - base;
    if (!WriteHeader(image, offset, &characteristics, sizeof(characteristics),
                     "DllCharacteristics", changes, error)) {
      return false;
    }
  }

  if (options.lower_os_version) {
    const uint16_t major = kWindows7Version >> 8;
    const uint16_t minor = kWindows7Version & 0xFF;
    struct Field {
      WORD* value;
      uint16_t wanted;
      const char* what;
    };
    const Field fields[] = {
        {&optional.MajorOperatingSystemVersion, major, "MajorOSVersion"},
        {&optional.MinorOperatingSystemVersion, minor, "MinorOSVersion"},
        {&optional.MajorSubsystemVersion, major, "MajorSubsystemVersion"},
        {&optional.MinorSubsystemVersion, minor, "MinorSubsystemVersion"},
    };
    for (const Field& field : fields) {
      if (*field.value == field.wanted) {
        continue;
      }
      const size_t offset =
          reinterpret_cast<const uint8_t*>(field.value) - base;
      if (!WriteHeader(image, offset, &field.wanted, sizeof(field.wanted),
                       field.what, changes, error)) {
        return false;
      }
    }
  }
  return true;
}

// Shortens every occurrence of the UWP FMOD module name where it stands.
//
// Shortening in place is deliberate. The name is reached through a `push
// offset` in code rather than through an import descriptor, so moving it would
// mean patching every reference; keeping the start address and terminating the
// string early costs nothing.
bool ShortenFmodName(pe::Image& image, std::vector<Change>* changes,
                     std::string* error) {
  const size_t old_length = sizeof(kFmodUwpName) - 1;
  const size_t new_length = sizeof(kFmodName) - 1;
  std::vector<uint8_t> replacement(old_length, 0);
  std::memcpy(replacement.data(), kFmodName, new_length);

  int found = 0;
  const IMAGE_SECTION_HEADER* section = image.sections();
  for (WORD i = 0; i < image.section_count(); ++i, ++section) {
    if (section->SizeOfRawData < old_length) {
      continue;
    }
    for (uint32_t at = 0; at + old_length <= section->SizeOfRawData; ++at) {
      uint8_t* const cursor =
          image.bytes().data() + section->PointerToRawData + at;
      if (std::memcmp(cursor, kFmodUwpName, old_length) != 0) {
        continue;
      }
      const uint32_t rva = section->VirtualAddress + at;
      if (!WriteAt(image, rva, replacement.data(), replacement.size(),
                   "fmod module name", changes, error)) {
        return false;
      }
      ++found;
    }
  }
  if (found == 0) {
    *error = "the UWP fmod module name was not found";
    return false;
  }
  return true;
}

// Retained descriptors (currently KERNEL32) must keep every original INT
// entry resolvable on Win7. Redirected slots are temporarily repointed to a
// harmless name from the same source module; the synthesized descriptor later
// overwrites the original IAT slot with the requested host/shim export.
bool NeutraliseRetainedRedirects(pe::Image& image, const ImportPlan& plan,
                                 std::vector<Change>* changes,
                                 std::string* error) {
  for (const ImportRedirect& redirect : plan.redirects) {
    if (redirect.neutralize_name_rva == 0) {
      continue;
    }
    if (!WriteAt(image, redirect.int_entry_rva, &redirect.neutralize_name_rva,
                 sizeof(redirect.neutralize_name_rva),
                 "neutralized source import", changes, error)) {
      return false;
    }
  }
  return true;
}

bool PatchBootstrapCall(pe::Image& image, uint32_t rva, uint32_t iat_va,
                        std::vector<Change>* changes, std::string* error) {
  const uint8_t* const site = image.At(rva, kBootstrapCallSize);
  if (site == nullptr) {
    *error = "bootstrap call site is not backed by file bytes";
    return false;
  }

  // push eax / push edi / push <ImageBase> / call rel32
  const uint32_t base = image.image_base();
  uint8_t expected[7] = {0x50, 0x57, 0x68};
  std::memcpy(expected + 3, &base, sizeof(base));
  if (std::memcmp(site, expected, sizeof(expected)) != 0 || site[7] != 0xE8) {
    *error = "bootstrap call site does not hold the expected handoff sequence";
    return false;
  }

  uint8_t replacement[kBootstrapCallSize];
  std::memset(replacement, 0x90, sizeof(replacement));
  replacement[0] = 0xFF;  // call dword ptr [imm32]
  replacement[1] = 0x15;
  std::memcpy(replacement + 2, &iat_va, sizeof(iat_va));
  return WriteAt(image, rva, replacement, sizeof(replacement),
                 "bootstrap call site", changes, error);
}

}  // namespace

const Target* FindTarget(const pe::Image& image) {
  const uint32_t stamp = image.nt()->FileHeader.TimeDateStamp;
  for (const Target& target : kTargets) {
    if (target.time_date_stamp == stamp) {
      return &target;
    }
  }
  return nullptr;
}

bool Patch(pe::Image& image, const Target& target, const Options& options,
           std::vector<Change>* changes, std::string* error) {
  if (image.nt()->FileHeader.TimeDateStamp != target.time_date_stamp) {
    *error = "image does not carry the target build stamp";
    return false;
  }
  const uint32_t stock_image_size = image.nt()->OptionalHeader.SizeOfImage;
  if (stock_image_size != target.image_size) {
    *error = "image has already been patched, or is not the stock build";
    return false;
  }

  ImportView view;
  if (!CollectImports(image, &view, error)) {
    return false;
  }
  ImportPlan import_plan;
  if (!BuildImportPlan(view.modules, options.shim_dll, &import_plan, error)) {
    return false;
  }

  // Everything the section holds has to be addressable before it exists, so
  // its RVA is predicted and the two published offsets are honoured by the
  // layout rather than discovered from it.
  const uint32_t section_rva = pe::NextSectionRva(image);
  const uint32_t bootstrap_iat_va =
      image.image_base() + section_rva + kBootstrapIatOffset;

  if (!RelaxHeader(image, options, changes, error) ||
      !ShortenFmodName(image, changes, error) ||
      !NeutraliseRetainedRedirects(image, import_plan, changes, error) ||
      !PatchBootstrapCall(image, target.bootstrap_call_rva, bootstrap_iat_va,
                          changes, error)) {
    return false;
  }

  // Recorded before the directory moves, so the journal can put it back.
  IMAGE_DATA_DIRECTORY& directory =
      image.nt()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  const IMAGE_DATA_DIRECTORY wanted = {section_rva + kDescriptorArrayOffset,
                                       DescriptorArrayBytes(view, import_plan)};
  const size_t directory_offset =
      reinterpret_cast<const uint8_t*>(&directory) - image.bytes().data();
  if (!WriteHeader(image, directory_offset, &wanted, sizeof(wanted),
                   "import directory", changes, error)) {
    return false;
  }

  std::vector<uint8_t> content;
  if (!BuildSectionContent(view, import_plan, options, section_rva,
                           target.time_date_stamp, stock_image_size,
                           target.bootstrap_call_rva, *changes, &content,
                           error)) {
    return false;
  }

  uint32_t placed = 0;
  const char name[8] = {'.', 'r', 'e', 'u', 'w', 'p', '\0', '\0'};
  if (!image.AppendSection(name,
                           IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                               IMAGE_SCN_MEM_WRITE,
                           content, &placed, error)) {
    return false;
  }
  if (placed != section_rva) {
    *error = "the section did not land where its contents were told it would";
    return false;
  }
  return true;
}

}  // namespace reuwp
