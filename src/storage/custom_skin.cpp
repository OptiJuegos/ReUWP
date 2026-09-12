#include "shim/custom_skin.h"

#include <commdlg.h>
#include <string.h>

#include "shim/app_window.h"
#include "shim/input.h"
#include "shim/log.h"
#include "shim/storage.h"
#include "shim/text_patch.h"

namespace shim::custom_skin {
namespace {

constexpr size_t kPathChars = 0x104;
constexpr DWORD kPickerFlags = 0x0008180C;
constexpr DWORD kCopyBlockSize = 0x400;
constexpr DWORD kMaxOptionsBytes = 0x100000;

constexpr wchar_t kSkinFilter[] =
    L"PNG skin (*.png)\0*.png\0All files (*.*)\0*.*\0";
constexpr wchar_t kPickerTitle[] = L"Choose a Minecraft skin PNG";
constexpr wchar_t kDefaultExtension[] = L"png";

constexpr wchar_t kMinecraftPeSkinSuffix[] =
    L"\\games\\com.mojang\\minecraftpe\\custom.png";
constexpr wchar_t kRootSkinSuffix[] = L"\\custom.png";
constexpr wchar_t kOptionsSuffix[] =
    L"\\games\\com.mojang\\minecraftpe\\options.txt";

constexpr unsigned char kPngSignature[8] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

constexpr char kSkinTypeKey[] = "game_skintypefull:";
constexpr char kSkinTypeLine[] = "game_skintypefull:Standard_Custom";
constexpr char kLastSkinKey[] = "game_lastcustomskinnew:";
constexpr char kLastSkinLine[] = "game_lastcustomskinnew:custom.png";

bool BuildLocalPath(const wchar_t* suffix,
                    wchar_t (&destination)[kPathChars]) noexcept {
  const wchar_t* const base = storage::LocalFolder();
  if (base == nullptr || base[0] == L'\0' || suffix == nullptr) {
    return false;
  }

  const int base_length = ::lstrlenW(base);
  const int suffix_length = ::lstrlenW(suffix);
  if (base_length < 0 || suffix_length < 0 ||
      static_cast<size_t>(base_length) + static_cast<size_t>(suffix_length) >=
          kPathChars) {
    return false;
  }

  ::lstrcpyW(destination, base);
  ::lstrcatW(destination, suffix);
  return true;
}

bool WriteAll(HANDLE file, const void* data, DWORD size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  DWORD done = 0;
  while (done < size) {
    DWORD written = 0;
    if (!::WriteFile(file, bytes + done, size - done, &written, nullptr) ||
        written == 0) {
      return false;
    }
    done += written;
  }
  return true;
}

bool CopyValidatedPng(const wchar_t* source_path,
                      const wchar_t* destination_path) noexcept {
  HANDLE source = ::CreateFileW(
      source_path, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (source == INVALID_HANDLE_VALUE) {
    return false;
  }

  unsigned char signature[sizeof(kPngSignature)] = {};
  DWORD read = 0;
  const bool header_ok =
      ::ReadFile(source, signature, static_cast<DWORD>(sizeof(signature)), &read,
                 nullptr) != FALSE &&
      read == sizeof(signature) &&
      memcmp(signature, kPngSignature, sizeof(kPngSignature)) == 0;
  if (!header_ok) {
    ::CloseHandle(source);
    return false;
  }

  HANDLE destination =
      ::CreateFileW(destination_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (destination == INVALID_HANDLE_VALUE) {
    ::CloseHandle(source);
    return false;
  }

  bool success = WriteAll(destination, signature, sizeof(signature));
  unsigned char buffer[kCopyBlockSize];
  while (success) {
    read = 0;
    if (!::ReadFile(source, buffer, sizeof(buffer), &read, nullptr)) {
      success = false;
      break;
    }
    if (read == 0) {
      break;
    }
    if (!WriteAll(destination, buffer, read)) {
      success = false;
      break;
    }
  }

  ::CloseHandle(destination);
  ::CloseHandle(source);
  return success;
}

bool ReadOptions(const wchar_t* path, void** data, DWORD* size) noexcept {
  if (data == nullptr || size == nullptr) {
    return false;
  }
  *data = nullptr;
  *size = 0;

  HANDLE file = ::CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  LARGE_INTEGER file_size = {};
  if (!::GetFileSizeEx(file, &file_size) || file_size.QuadPart < 0 ||
      file_size.QuadPart > kMaxOptionsBytes) {
    ::CloseHandle(file);
    return false;
  }

  const DWORD bytes = static_cast<DWORD>(file_size.QuadPart);
  void* buffer = ::HeapAlloc(::GetProcessHeap(), 0, bytes != 0 ? bytes : 1);
  if (buffer == nullptr) {
    ::CloseHandle(file);
    return false;
  }

  DWORD total = 0;
  while (total < bytes) {
    DWORD chunk = 0;
    if (!::ReadFile(file, static_cast<unsigned char*>(buffer) + total,
                    bytes - total, &chunk, nullptr) ||
        chunk == 0) {
      ::HeapFree(::GetProcessHeap(), 0, buffer);
      ::CloseHandle(file);
      return false;
    }
    total += chunk;
  }

  ::CloseHandle(file);
  *data = buffer;
  *size = bytes;
  return true;
}

bool WriteOptions(const wchar_t* path, const void* data, size_t size) noexcept {
  if (size > kMaxOptionsBytes || size > MAXDWORD) {
    return false;
  }

  HANDLE file = ::CreateFileW(path, GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  const bool success =
      WriteAll(file, data, static_cast<DWORD>(size));
  ::CloseHandle(file);
  return success;
}

bool UpdateOptions(const wchar_t* path) noexcept {
  void* original = nullptr;
  DWORD original_size = 0;
  if (!ReadOptions(path, &original, &original_size)) {
    return false;
  }

  util::PatchedText first = {};
  util::PatchedText second = {};
  const bool first_ok = util::ReplaceLine(
      original, original_size, kSkinTypeKey, sizeof(kSkinTypeKey) - 1,
      kSkinTypeLine, sizeof(kSkinTypeLine) - 1, &first);
  ::HeapFree(::GetProcessHeap(), 0, original);
  if (!first_ok) {
    return false;
  }

  const bool second_ok = util::ReplaceLine(
      first.data, first.size, kLastSkinKey, sizeof(kLastSkinKey) - 1,
      kLastSkinLine, sizeof(kLastSkinLine) - 1, &second);
  first.Release();
  if (!second_ok) {
    return false;
  }

  const bool written = WriteOptions(path, second.data, second.size);
  second.Release();
  return written;
}

}  // namespace

bool ShowAndInstall() noexcept {
  wchar_t selected[kPathChars] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = 0x58;
  ofn.hwndOwner = app_window::Handle();
  ofn.lpstrFilter = kSkinFilter;
  ofn.lpstrFile = selected;
  ofn.nMaxFile = 0x104;
  ofn.lpstrTitle = kPickerTitle;
  ofn.Flags = kPickerFlags;
  ofn.lpstrDefExt = kDefaultExtension;

  // FUN_6295BE50 abandona el modo relativo antes de entrar en el dialogo modal
  // y fuerza a recalcular el ClipCursor sobre la ventana de escritorio.
  input::RequestCursorMode(false);
  input::InvalidateCursorClip();
  input::SyncCursorMode(app_window::Handle());

  if (::GetOpenFileNameW(&ofn) == FALSE) {
    log::Write("Win32 custom-skin picker cancelled");
    return false;
  }

  wchar_t minecraftpe_skin[kPathChars] = {};
  wchar_t root_skin[kPathChars] = {};
  wchar_t options[kPathChars] = {};
  if (!BuildLocalPath(kMinecraftPeSkinSuffix, minecraftpe_skin) ||
      !BuildLocalPath(kRootSkinSuffix, root_skin) ||
      !BuildLocalPath(kOptionsSuffix, options)) {
    log::Write("custom skin destination path too long");
    return false;
  }

  // El original intenta las dos ubicaciones y considera la copia valida si al
  // menos una funciona. Ambas pasan por el mismo validador de firma PNG.
  const bool copied_minecraftpe = CopyValidatedPng(selected, minecraftpe_skin);
  const bool copied_root = CopyValidatedPng(selected, root_skin);
  if (!copied_minecraftpe && !copied_root) {
    log::Write("failed to copy selected custom skin PNG");
    return false;
  }

  if (!UpdateOptions(options)) {
    log::Write("custom skin copied, but options update failed");
    return false;
  }

  log::Write("Win32 custom skin PNG installed as custom.png");
  return true;
}

}  // namespace shim::custom_skin
