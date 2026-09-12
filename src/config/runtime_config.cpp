#include "shim/runtime_config.h"

#include "shim/branding.h"
#include "shim/log.h"

namespace shim::runtime_config {
namespace {

constexpr wchar_t kConfigFileName[] = SHIM_DISPLAY_NAME_W L".ini";
constexpr char kDefaultConfig[] =
    "LocalSavePath=0\r\n"
    "MinRenderDistance=1\r\n";
constexpr DWORD kMaxConfigBytes = 4096;

Settings g_settings = {};
wchar_t g_file_path[MAX_PATH] = {};
bool g_initialized = false;

bool BuildConfigPath(const wchar_t* install_directory) noexcept {
  if (install_directory == nullptr || install_directory[0] == L'\0') {
    return false;
  }

  const int directory_length = ::lstrlenW(install_directory);
  const int file_name_length = ::lstrlenW(kConfigFileName);
  if (directory_length + 1 + file_name_length >= MAX_PATH) {
    return false;
  }

  ::lstrcpyW(g_file_path, install_directory);
  ::lstrcatW(g_file_path, L"\\");
  ::lstrcatW(g_file_path, kConfigFileName);
  return true;
}

bool WriteDefaultConfig() noexcept {
  HANDLE file = ::CreateFileW(g_file_path, GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return ::GetLastError() == ERROR_FILE_EXISTS;
  }

  DWORD written = 0;
  const bool ok =
      ::WriteFile(file, kDefaultConfig,
                  static_cast<DWORD>(sizeof(kDefaultConfig) - 1), &written,
                  nullptr) != FALSE &&
      written == sizeof(kDefaultConfig) - 1;
  ::CloseHandle(file);
  return ok;
}

bool ParseUnsigned(const char* begin, const char* end,
                   unsigned int* value) noexcept {
  if (begin == nullptr || end == nullptr || value == nullptr || begin >= end) {
    return false;
  }

  unsigned int parsed = 0;
  const char* cursor = begin;
  while (cursor < end && (*cursor == ' ' || *cursor == '\t')) {
    ++cursor;
  }
  if (cursor == end) {
    return false;
  }

  bool has_digit = false;
  while (cursor < end && *cursor >= '0' && *cursor <= '9') {
    has_digit = true;
    const unsigned int digit = static_cast<unsigned int>(*cursor - '0');
    if (parsed > (0xFFFFFFFFu - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
    ++cursor;
  }

  while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                          *cursor == '\r')) {
    ++cursor;
  }
  if (!has_digit || cursor != end) {
    return false;
  }

  *value = parsed;
  return true;
}

bool KeyEquals(const char* begin, const char* end, const char* key) noexcept {
  if (begin == nullptr || end == nullptr || key == nullptr) {
    return false;
  }

  const char* cursor = begin;
  while (cursor < end && *cursor == *key && *key != '\0') {
    ++cursor;
    ++key;
  }
  return cursor == end && *key == '\0';
}

void ParseConfig(const char* data, DWORD size) noexcept {
  const char* cursor = data;
  const char* const finish = data + size;

  while (cursor < finish) {
    const char* line_end = cursor;
    while (line_end < finish && *line_end != '\n') {
      ++line_end;
    }

    const char* equals = cursor;
    while (equals < line_end && *equals != '=') {
      ++equals;
    }

    if (equals < line_end) {
      const char* key_end = equals;
      while (key_end > cursor &&
             (key_end[-1] == ' ' || key_end[-1] == '\t')) {
        --key_end;
      }

      unsigned int value = 0;
      if (ParseUnsigned(equals + 1, line_end, &value)) {
        if (KeyEquals(cursor, key_end, "LocalSavePath")) {
          if (value <= 1u) {
            g_settings.local_save_path = value != 0u;
          }
        } else if (KeyEquals(cursor, key_end, "MinRenderDistance")) {
          if (value >= 1u && value <= 4u) {
            g_settings.min_render_distance = value;
          }
        }
      }
    }

    cursor = line_end < finish ? line_end + 1 : finish;
  }
}

void ReadConfig() noexcept {
  HANDLE file = ::CreateFileW(g_file_path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    log::Write("runtime config could not be opened; defaults remain active");
    return;
  }

  char data[kMaxConfigBytes] = {};
  DWORD bytes_read = 0;
  if (::ReadFile(file, data, kMaxConfigBytes, &bytes_read, nullptr) &&
      bytes_read != 0) {
    ParseConfig(data, bytes_read);
  }
  ::CloseHandle(file);
}

}  // namespace

bool Initialize(const wchar_t* install_directory) noexcept {
  if (g_initialized) {
    return g_file_path[0] != L'\0';
  }
  g_initialized = true;

  if (!BuildConfigPath(install_directory)) {
    log::Write("runtime config path could not be built");
    return false;
  }

  if (::GetFileAttributesW(g_file_path) == INVALID_FILE_ATTRIBUTES) {
    if (!WriteDefaultConfig()) {
      log::Write("runtime config default file could not be created");
    }
  }

  ReadConfig();
  log::Writef("runtime config: LocalSavePath=%u MinRenderDistance=%u",
              g_settings.local_save_path ? 1u : 0u,
              g_settings.min_render_distance);
  return true;
}

const Settings& Get() noexcept {
  return g_settings;
}

const wchar_t* FilePath() noexcept {
  return g_file_path;
}

}  // namespace shim::runtime_config
