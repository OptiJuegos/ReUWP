#pragma once

// Contract between the patcher and the shim.
//
// Deliberately free of windows.h and of the rest of shim/: the patcher is a
// host tool and includes this same header, so keeping it self-contained is what
// stops the two sides from drifting apart.

#include <stddef.h>
#include <stdint.h>

namespace shim::game {

// Section the patcher appends to the game executable. An IMAGE_SECTION_HEADER
// holds eight bytes of name, so this is the whole budget.
inline constexpr char kImageInfoSection[8] = {'.', 'r', 'e', 'u', 'w', 'p',
                                              '\0', '\0'};

inline constexpr char kImageInfoMagic[8] = {'R', 'E', 'U', 'W', 'P',
                                            'I', 'M', 'G'};

// Bump on any layout change instead of editing fields in place. A mismatched
// patcher/shim pair is then reported rather than silently misread, which is the
// failure mode this whole block exists to remove.
inline constexpr uint32_t kImageInfoFormat = 1;

// Written at the start of the section by the patcher, read by the shim.
//
// SizeOfImage used to carry build identity and it identified the *patcher*
// rather than the game: it moves with the patch geometry, which is why a
// patched 1.1.5 fingerprints as 0x01587000 while a patched 0.15.10 keeps its
// stock value. TimeDateStamp comes from Mojang's build and is identical before
// and after patching.
struct ImageInfo {
  char magic[8];             // kImageInfoMagic, not NUL-terminated
  uint32_t format;           // kImageInfoFormat
  uint32_t header_size;      // sizeof(ImageInfo)
  uint32_t game_time_stamp;  // IMAGE_FILE_HEADER::TimeDateStamp, stock value
  uint32_t game_image_size;  // OptionalHeader::SizeOfImage BEFORE patching
  uint32_t patcher_version;
  uint32_t bootstrap_rva;    // where the call to Win32Bootstrap was written
  uint32_t journal_rva;      // original bytes of each patched range, 0 if none
  uint32_t journal_count;
  uint32_t checksum;         // ImageInfoChecksum(), always the last field
};

static_assert(sizeof(ImageInfo) == 44,
              "ImageInfo is a file format; changing its size breaks images "
              "already patched with this format number");

// One original range recorded by the patcher, so a patched image can be taken
// back to its stock bytes.
struct ImageInfoJournalEntry {
  uint32_t rva;
  uint32_t size;
  uint32_t data_rva;  // where the original bytes live inside the section
  uint32_t reserved;
};

static_assert(sizeof(ImageInfoJournalEntry) == 16,
              "journal entries are a file format");

// FNV-1a over every field before `checksum`.
//
// Trivial on purpose and defined here so both sides run the same code: this
// catches a truncated or half-written block, not tampering.
inline uint32_t ImageInfoChecksum(const ImageInfo& info) noexcept {
  const auto* const bytes = reinterpret_cast<const unsigned char*>(&info);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(ImageInfo, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

inline bool ImageInfoHasMagic(const ImageInfo& info) noexcept {
  for (size_t i = 0; i < sizeof(info.magic); ++i) {
    if (info.magic[i] != kImageInfoMagic[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace shim::game
