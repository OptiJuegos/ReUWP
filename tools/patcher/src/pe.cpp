#include "pe.h"

#include <cstdio>
#include <cstring>

namespace reuwp::pe {
namespace {

constexpr size_t kMinHeaderRoom = sizeof(IMAGE_SECTION_HEADER);

}  // namespace

uint32_t AlignUp(uint32_t value, uint32_t alignment) noexcept {
  if (alignment == 0) {
    return value;
  }
  return (value + alignment - 1) / alignment * alignment;
}

uint32_t NextSectionRva(const Image& image) noexcept {
  uint32_t end_rva = 0;
  const IMAGE_SECTION_HEADER* section = image.sections();
  for (WORD i = 0; i < image.section_count(); ++i, ++section) {
    const uint32_t section_end =
        section->VirtualAddress + section->Misc.VirtualSize;
    if (section_end > end_rva) {
      end_rva = section_end;
    }
  }
  return AlignUp(end_rva, image.nt()->OptionalHeader.SectionAlignment);
}

bool Image::Load(const std::string& path, std::string* error) {
  FILE* file = nullptr;
  if (::fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr) {
    *error = "cannot open " + path;
    return false;
  }
  ::fseek(file, 0, SEEK_END);
  const long size = ::ftell(file);
  ::fseek(file, 0, SEEK_SET);
  if (size <= 0) {
    ::fclose(file);
    *error = path + " is empty";
    return false;
  }
  bytes_.resize(static_cast<size_t>(size));
  const size_t read = ::fread(bytes_.data(), 1, bytes_.size(), file);
  ::fclose(file);
  if (read != bytes_.size()) {
    *error = "short read on " + path;
    return false;
  }
  return Parse(error);
}

bool Image::Parse(std::string* error) const {
  if (bytes_.size() < sizeof(IMAGE_DOS_HEADER)) {
    *error = "file is too small to be a PE";
    return false;
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes_.data());
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    *error = "missing MZ signature";
    return false;
  }
  const size_t offset = static_cast<size_t>(dos->e_lfanew);
  if (offset + sizeof(IMAGE_NT_HEADERS32) > bytes_.size()) {
    *error = "PE header runs past the end of the file";
    return false;
  }
  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes_.data() + offset);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    *error = "missing PE signature";
    return false;
  }
  if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    *error = "not a 32-bit x86 PE";
    return false;
  }
  nt_offset_ = offset;
  return true;
}

bool Image::Save(const std::string& path, std::string* error) const {
  FILE* file = nullptr;
  if (::fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr) {
    *error = "cannot create " + path;
    return false;
  }
  const size_t written = ::fwrite(bytes_.data(), 1, bytes_.size(), file);
  ::fclose(file);
  if (written != bytes_.size()) {
    *error = "short write on " + path;
    return false;
  }
  return true;
}

IMAGE_NT_HEADERS32* Image::nt() noexcept {
  return reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes_.data() + nt_offset_);
}

const IMAGE_NT_HEADERS32* Image::nt() const noexcept {
  return reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes_.data() +
                                                     nt_offset_);
}

IMAGE_SECTION_HEADER* Image::sections() noexcept {
  return IMAGE_FIRST_SECTION(nt());
}

const IMAGE_SECTION_HEADER* Image::sections() const noexcept {
  return IMAGE_FIRST_SECTION(nt());
}

WORD Image::section_count() const noexcept {
  return nt()->FileHeader.NumberOfSections;
}

uint32_t Image::image_base() const noexcept {
  return nt()->OptionalHeader.ImageBase;
}

bool Image::RvaToOffset(uint32_t rva, size_t* offset) const noexcept {
  const IMAGE_SECTION_HEADER* section = sections();
  for (WORD i = 0; i < section_count(); ++i, ++section) {
    const uint32_t start = section->VirtualAddress;
    // Only the file-backed part: the tail between SizeOfRawData and
    // VirtualSize exists at run time but has no bytes here.
    if (rva >= start && rva < start + section->SizeOfRawData) {
      *offset = section->PointerToRawData + (rva - start);
      return true;
    }
  }
  return false;
}

uint8_t* Image::At(uint32_t rva, size_t size) noexcept {
  size_t offset = 0;
  if (!RvaToOffset(rva, &offset) || offset + size > bytes_.size()) {
    return nullptr;
  }
  return bytes_.data() + offset;
}

const uint8_t* Image::At(uint32_t rva, size_t size) const noexcept {
  return const_cast<Image*>(this)->At(rva, size);
}

bool Image::AppendSection(const char (&name)[8], uint32_t characteristics,
                          const std::vector<uint8_t>& data, uint32_t* rva,
                          std::string* error) {
  IMAGE_NT_HEADERS32* header = nt();
  const uint32_t section_alignment = header->OptionalHeader.SectionAlignment;
  const uint32_t file_alignment = header->OptionalHeader.FileAlignment;

  // The section table has to grow by one entry, and it lives between the
  // optional header and the first section's raw data. There is no room to make.
  const IMAGE_SECTION_HEADER* first = sections();
  const size_t table_end =
      reinterpret_cast<const uint8_t*>(first + section_count()) -
      bytes_.data();
  uint32_t first_raw = 0xFFFFFFFFu;
  uint32_t end_rva = 0;
  const IMAGE_SECTION_HEADER* section = sections();
  for (WORD i = 0; i < section_count(); ++i, ++section) {
    if (section->SizeOfRawData != 0 && section->PointerToRawData < first_raw) {
      first_raw = section->PointerToRawData;
    }
    const uint32_t section_end =
        section->VirtualAddress + section->Misc.VirtualSize;
    if (section_end > end_rva) {
      end_rva = section_end;
    }
  }
  if (first_raw == 0xFFFFFFFFu || table_end + kMinHeaderRoom > first_raw) {
    *error = "no room in the header for another section entry";
    return false;
  }

  const uint32_t new_rva = AlignUp(end_rva, section_alignment);
  const uint32_t raw_size =
      AlignUp(static_cast<uint32_t>(data.size()), file_alignment);
  const uint32_t new_raw =
      AlignUp(static_cast<uint32_t>(bytes_.size()), file_alignment);

  bytes_.resize(new_raw, 0);
  bytes_.insert(bytes_.end(), data.begin(), data.end());
  bytes_.resize(static_cast<size_t>(new_raw) + raw_size, 0);

  // bytes_ moved, so every cached pointer is stale from here on.
  header = nt();
  IMAGE_SECTION_HEADER* entry = sections() + section_count();
  std::memset(entry, 0, sizeof(*entry));
  std::memcpy(entry->Name, name, sizeof(entry->Name));
  entry->Misc.VirtualSize = static_cast<DWORD>(data.size());
  entry->VirtualAddress = new_rva;
  entry->SizeOfRawData = raw_size;
  entry->PointerToRawData = new_raw;
  entry->Characteristics = characteristics;

  header->FileHeader.NumberOfSections =
      static_cast<WORD>(header->FileHeader.NumberOfSections + 1);
  header->OptionalHeader.SizeOfImage =
      AlignUp(new_rva + static_cast<uint32_t>(data.size()), section_alignment);
  header->OptionalHeader.SizeOfInitializedData += raw_size;

  *rva = new_rva;
  return true;
}

}  // namespace reuwp::pe
