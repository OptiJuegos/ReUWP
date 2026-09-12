#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pe.h"

namespace reuwp {

// A build this patcher knows how to handle.
//
// Identity is the stock TimeDateStamp: it comes from Mojang's build and, unlike
// SizeOfImage, does not move when the image is patched.
struct Target {
  const char* name;
  uint32_t time_date_stamp;
  uint32_t image_size;          // stock SizeOfImage, cross-check only
  uint32_t bootstrap_call_rva;  // 0 to locate it by signature
};

struct Options {
  std::string shim_dll = "reuwp.dll";
  bool lower_os_version = true;  // 6.2 -> 6.1, needed only for Windows 7
  bool keep_aslr = false;        // leave DYNAMIC_BASE alone
  uint32_t patcher_version = 1;
};

// One range the patcher rewrote. Doubles as the dry-run report and as the
// journal that goes into the image, so an image can be taken back to stock.
struct Change {
  uint32_t rva;  // 0 for header edits, which are addressed by file offset
  uint32_t file_offset;
  std::vector<uint8_t> original;
  std::vector<uint8_t> patched;
  std::string what;
};

const Target* FindTarget(const pe::Image& image);

// Applies the whole patch to `image` in memory. On failure nothing is written
// to disk, because the caller holds the only copy and never saves it.
bool Patch(pe::Image& image, const Target& target, const Options& options,
           std::vector<Change>* changes, std::string* error);

}  // namespace reuwp
