// Turns a stock Minecraft UWP executable into one that loads the shim.
//
// The input file is never modified: it is read into memory, patched there and
// written to a separate output. A dry run stops before the write and prints
// every byte it would have changed, which is the only way to review a patch
// against an image nobody can rebuild.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "patcher.h"
#include "pe.h"

namespace {

void PrintUsage() {
  std::printf(
      "usage: reuwp_patch <input.exe> [-o <output.exe>] [options]\n"
      "\n"
      "  -o, --output <path>   output path (default: <input>_reuwp.exe)\n"
      "      --shim <name>     module name to import (default: reuwp.dll)\n"
      "      --dry-run         report the changes without writing anything\n"
      "      --keep-aslr       leave DYNAMIC_BASE set\n"
      "      --no-win7         leave the OS version fields alone\n");
}

bool EndsWithReuwp(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  constexpr const char* kSuffix = "_reuwp";
  constexpr size_t kSuffixLength = 6;
  return value.size() >= kSuffixLength &&
         value.compare(value.size() - kSuffixLength, kSuffixLength, kSuffix) ==
             0;
}

bool DefaultOutputPath(const std::string& input, std::string* output,
                       std::string* error) {
  if (output == nullptr) {
    return false;
  }

  const size_t slash = input.find_last_of("\\/");
  const size_t filename_begin = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = input.find_last_of('.');
  const bool has_extension =
      dot != std::string::npos && dot > filename_begin;
  const size_t stem_end = has_extension ? dot : input.size();
  const std::string stem =
      input.substr(filename_begin, stem_end - filename_begin);

  if (EndsWithReuwp(stem)) {
    if (error != nullptr) {
      *error = "input filename already ends with _reuwp; use -o to choose an "
               "explicit output path";
    }
    return false;
  }

  *output = input.substr(0, stem_end) + "_reuwp";
  if (has_extension) {
    output->append(input.substr(dot));
  }
  return true;
}

void PrintChange(const reuwp::Change& change) {
  std::printf("  %-28s rva=%08X off=%08X %u bytes\n", change.what.c_str(),
              change.rva, change.file_offset,
              static_cast<unsigned>(change.original.size()));
  const size_t shown = change.original.size() < 16 ? change.original.size() : 16;
  std::printf("      from:");
  for (size_t i = 0; i < shown; ++i) {
    std::printf(" %02X", change.original[i]);
  }
  std::printf("%s\n      to  :", shown < change.original.size() ? " ..." : "");
  for (size_t i = 0; i < shown; ++i) {
    std::printf(" %02X", change.patched[i]);
  }
  std::printf("%s\n", shown < change.patched.size() ? " ..." : "");
}

}  // namespace

int main(int argc, char** argv) {
  std::string input;
  std::string output;
  reuwp::Options options;
  bool dry_run = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-o" || arg == "--output") {
      if (++i >= argc) {
        PrintUsage();
        return 2;
      }
      output = argv[i];
    } else if (arg == "--shim") {
      if (++i >= argc) {
        PrintUsage();
        return 2;
      }
      options.shim_dll = argv[i];
    } else if (arg == "--dry-run") {
      dry_run = true;
    } else if (arg == "--keep-aslr") {
      options.keep_aslr = true;
    } else if (arg == "--no-win7") {
      options.lower_os_version = false;
    } else if (arg == "-h" || arg == "--help") {
      PrintUsage();
      return 0;
    } else if (input.empty()) {
      input = arg;
    } else {
      std::fprintf(stderr, "unexpected argument: %s\n", arg.c_str());
      return 2;
    }
  }

  if (input.empty()) {
    PrintUsage();
    return 2;
  }

  std::string error;
  if (!dry_run && output.empty()) {
    if (!DefaultOutputPath(input, &output, &error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 2;
    }
  }

  reuwp::pe::Image image;
  if (!image.Load(input, &error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  const reuwp::Target* const target = reuwp::FindTarget(image);
  if (target == nullptr) {
    std::fprintf(stderr,
                 "error: unsupported build (TimeDateStamp=%08X "
                 "SizeOfImage=%08X)\n",
                 image.nt()->FileHeader.TimeDateStamp,
                 image.nt()->OptionalHeader.SizeOfImage);
    return 1;
  }
  std::printf("target: Minecraft %s\n", target->name);

  std::vector<reuwp::Change> changes;
  if (!reuwp::Patch(image, *target, options, &changes, &error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  std::printf("changes: %u\n", static_cast<unsigned>(changes.size()));
  for (const reuwp::Change& change : changes) {
    PrintChange(change);
  }
  // The appended section is the last entry; NextSectionRva would already be
  // pointing past it by now.
  const IMAGE_SECTION_HEADER& added =
      image.sections()[image.section_count() - 1];
  std::printf("section: %-8.8s at rva %08X, %u bytes, SizeOfImage now %08X\n",
              reinterpret_cast<const char*>(added.Name), added.VirtualAddress,
              static_cast<unsigned>(added.Misc.VirtualSize),
              image.nt()->OptionalHeader.SizeOfImage);

  if (dry_run) {
    std::printf("dry run: nothing written\n");
    return 0;
  }
  if (!image.Save(output, &error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  std::printf("written: %s\n", output.c_str());
  return 0;
}
