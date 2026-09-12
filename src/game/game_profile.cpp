#include "shim/game_profile.h"

#include "shim/game_profiles.h"
#include "shim/image_info.h"
#include "shim/log.h"
#include "shim/memory.h"

#include <cstring>

namespace shim::game {
namespace {

HMODULE g_base = nullptr;
const VersionProfile* g_profile = nullptr;
bool g_is_windows7 = false;

const VersionProfile* const kProfiles[] = {
    &Profile115(),
    &Profile01510(),
    &Profile0132(),
};

// Returns the patcher's block if the image carries a readable one.
//
// Everything is validated before the contents are trusted: the section has to
// be large enough for the struct, the magic has to match, and the checksum has
// to cover the fields as written. A half-written block reads as absent, which
// falls through to the intrinsic fingerprints rather than misidentifying the
// build.
const ImageInfo* FindImageInfo(const unsigned char* bytes,
                               const IMAGE_NT_HEADERS32* nt) noexcept {
  const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
    bool named = true;
    for (size_t k = 0; k < sizeof(section->Name); ++k) {
      if (static_cast<char>(section->Name[k]) != kImageInfoSection[k]) {
        named = false;
        break;
      }
    }
    if (!named || section->Misc.VirtualSize < sizeof(ImageInfo)) {
      continue;
    }

    const auto* const info =
        reinterpret_cast<const ImageInfo*>(bytes + section->VirtualAddress);
    if (!ImageInfoHasMagic(*info) || info->header_size != sizeof(ImageInfo) ||
        info->checksum != ImageInfoChecksum(*info)) {
      log::Write("reuwp image block present but not readable; ignoring it");
      return nullptr;
    }
    return info;
  }
  return nullptr;
}

const VersionProfile* MatchTimeStamp(DWORD stamp) noexcept {
  if (stamp == 0) {
    return nullptr;
  }
  for (const VersionProfile* profile : kProfiles) {
    if (profile->time_date_stamp == stamp) {
      return profile;
    }
  }
  return nullptr;
}

bool MatchesIdentitySignatures(const VersionProfile& profile,
                               const unsigned char* image,
                               DWORD image_size) noexcept {
  if (profile.identity_signature_count == 0) {
    return true;
  }
  if (profile.identity_signatures == nullptr) {
    log::Writef("%s identity signature table is missing", profile.version_name);
    return false;
  }

  for (size_t index = 0; index < profile.identity_signature_count; ++index) {
    const CodeSignatureSite& site = profile.identity_signatures[index];
    if (site.size == 0 || site.size > sizeof(site.expected) ||
        site.rva >= image_size || site.size > image_size - site.rva) {
      log::Writef("%s identity signature has invalid bounds at rva=%08lx",
                  profile.version_name,
                  static_cast<unsigned long>(site.rva));
      return false;
    }

    const void* const target = image + site.rva;
    if (!memory::IsReadable(target, site.size) ||
        std::memcmp(target, site.expected, site.size) != 0) {
      log::Writef("%s identity signature mismatch at rva=%08lx",
                  profile.version_name,
                  static_cast<unsigned long>(site.rva));
      return false;
    }
  }
  return true;
}

const VersionProfile* ValidateMatchedProfile(const VersionProfile* profile,
                                             const unsigned char* image,
                                             DWORD image_size) noexcept {
  if (profile == nullptr) {
    return nullptr;
  }
  return MatchesIdentitySignatures(*profile, image, image_size) ? profile
                                                                : nullptr;
}

}  // namespace

const VersionProfile* DetectProfile(HMODULE module) noexcept {
  if (module == nullptr) {
    return nullptr;
  }

  const auto* bytes = reinterpret_cast<const unsigned char*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return nullptr;
  }

  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    return nullptr;
  }

  // Layered on purpose, most specific first.
  //
  // 1. The patcher's own block, which states the stock build identity outright.
  if (const ImageInfo* const info = FindImageInfo(bytes, nt)) {
    if (info->format != kImageInfoFormat) {
      // Loud, because this is the one failure the block exists to name: an
      // executable and a shim from different releases. Detection still falls
      // through, so a usable pair keeps working.
      log::Writef("reuwp image block is format %lu, this build understands %lu"
                  " - patcher and shim are from different releases",
                  static_cast<unsigned long>(info->format),
                  static_cast<unsigned long>(kImageInfoFormat));
    } else if (const VersionProfile* const matched =
                   MatchTimeStamp(info->game_time_stamp)) {
      return ValidateMatchedProfile(matched, bytes,
                                    nt->OptionalHeader.SizeOfImage);
    }
  }

  // 2. Mojang's build stamp, which survives patching untouched.
  if (const VersionProfile* const matched =
          MatchTimeStamp(nt->FileHeader.TimeDateStamp)) {
    return ValidateMatchedProfile(matched, bytes,
                                  nt->OptionalHeader.SizeOfImage);
  }

  // 3. SizeOfImage. Kept because it is what every executable distributed so far
  // is recognised by, and because 0.13.2 has no recorded stamp.
  for (const VersionProfile* profile : kProfiles) {
    if (profile->image_size == nt->OptionalHeader.SizeOfImage &&
        MatchesIdentitySignatures(*profile, bytes,
                                  nt->OptionalHeader.SizeOfImage)) {
      return profile;
    }
  }

  return nullptr;
}

Version Initialize(HMODULE game_module) noexcept {
  g_base = game_module;
  g_profile = DetectProfile(game_module);

  // An unrecognised build used to end as a silent exit code out of Run(), which
  // left nothing to go on. Print what was actually looked at.
  if (g_profile == nullptr && game_module != nullptr) {
    const auto* const bytes =
        reinterpret_cast<const unsigned char*>(game_module);
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
      const auto* const nt =
          reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes + dos->e_lfanew);
      if (nt->Signature == IMAGE_NT_SIGNATURE) {
        log::Writef("unsupported game build: TimeDateStamp=%08lx "
                    "SizeOfImage=%08lx",
                    static_cast<unsigned long>(nt->FileHeader.TimeDateStamp),
                    static_cast<unsigned long>(
                        nt->OptionalHeader.SizeOfImage));
      }
    }
  }

  g_is_windows7 = false;
  if (const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll")) {
    using GetVersionFn = DWORD(WINAPI*)();
    const auto get_version = reinterpret_cast<GetVersionFn>(
        ::GetProcAddress(kernel32, "GetVersion"));
    if (get_version != nullptr) {
      g_is_windows7 = static_cast<unsigned short>(get_version()) == 0x0106;
    }
  }

  return Current();
}

HMODULE Base() noexcept {
  return g_base;
}

Version Current() noexcept {
  return g_profile != nullptr ? g_profile->version : Version::kUnknown;
}

bool IsWindows7() noexcept {
  return g_is_windows7;
}

const VersionProfile* CurrentProfile() noexcept {
  return g_profile;
}

const char* VersionName() noexcept {
  return g_profile != nullptr ? g_profile->version_name : "unknown";
}

bool HasCapability(VersionCapability capability) noexcept {
  return g_profile != nullptr &&
         HasCapability(g_profile->capabilities, capability);
}

}  // namespace shim::game
