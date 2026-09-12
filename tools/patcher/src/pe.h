#pragma once

// Minimal PE32 model over an in-memory copy of the file.
//
// The whole image is loaded, modified and written back out to a new file. It is
// never patched in place: a half-written executable is indistinguishable from a
// correctly patched one until it fails to start.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace reuwp::pe {

class Image {
 public:
  bool Load(const std::string& path, std::string* error);
  bool Save(const std::string& path, std::string* error) const;

  IMAGE_NT_HEADERS32* nt() noexcept;
  const IMAGE_NT_HEADERS32* nt() const noexcept;

  IMAGE_SECTION_HEADER* sections() noexcept;
  const IMAGE_SECTION_HEADER* sections() const noexcept;
  WORD section_count() const noexcept;

  uint32_t image_base() const noexcept;

  // Translates an RVA to a file offset. Fails for RVAs that are mapped but have
  // no bytes in the file, which is why every caller has to check: writing into
  // a section's uninitialised tail would silently land past the end.
  bool RvaToOffset(uint32_t rva, size_t* offset) const noexcept;

  // Returns a pointer to `size` file-backed bytes at `rva`, or nullptr.
  uint8_t* At(uint32_t rva, size_t size) noexcept;
  const uint8_t* At(uint32_t rva, size_t size) const noexcept;

  // Appends a section carrying `data` and returns its RVA. Grows SizeOfImage,
  // SizeOfInitializedData and NumberOfSections to match.
  bool AppendSection(const char (&name)[8], uint32_t characteristics,
                     const std::vector<uint8_t>& data, uint32_t* rva,
                     std::string* error);

  std::vector<uint8_t>& bytes() noexcept { return bytes_; }
  const std::vector<uint8_t>& bytes() const noexcept { return bytes_; }

 private:
  bool Parse(std::string* error) const;

  std::vector<uint8_t> bytes_;
  mutable size_t nt_offset_ = 0;
};

// Rounds `value` up to a multiple of `alignment`.
uint32_t AlignUp(uint32_t value, uint32_t alignment) noexcept;

// RVA that AppendSection would hand out, without appending anything. The
// section's own contents have to reference it, so it has to be known first.
uint32_t NextSectionRva(const Image& image) noexcept;

}  // namespace reuwp::pe
