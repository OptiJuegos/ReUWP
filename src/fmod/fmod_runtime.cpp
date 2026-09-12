#include "fmod_internal.h"

#include "shim/app_window.h"
#include "shim/log.h"

namespace shim::fmod::detail {
namespace {

struct OutputChain {
  int values[4];
  int count;
};

OutputChain BuildOutputChain(OutputType requested) noexcept {
  switch (requested) {
    case OutputType::kWasapi:
      return {{8, 6, 7, 0}, 3};
    case OutputType::kDSound:
      return {{6, 8, 7, 0}, 3};
    case OutputType::kWinMM:
      return {{7, 8, 6, 0}, 3};
    default:
      return {{static_cast<int>(requested), 8, 6, 7}, 4};
  }
}

void SelectFirstDriver(System* system, int output) noexcept {
  Api& api = ApiTable();
  if (api.getNumDrivers == nullptr) {
    return;
  }

  int count = -1;
  const Result result = api.getNumDrivers(system, &count);
  log::Writef("FMOD bridge: pre-init drivers output=%s result=%d count=%d",
              OutputTypeName(output), result, count);

  if (result != kOk || count <= 0 || api.setDriver == nullptr) {
    return;
  }

  const Result driver_result = api.setDriver(system, 0);
  log::Writef("FMOD bridge: %s -> %d (%s)", "setDriver(0)", driver_result,
              ErrorText(driver_result));
}

void LogCreateResult(const char* label, Result result, unsigned int mode,
                     Sound** sound, const char* normalized) noexcept {
  if (!ShouldLogSoundCreate(result)) {
    return;
  }

  char safe_path[420] = {};
  ::lstrcpynA(safe_path, normalized != nullptr ? normalized : "(null)",
              static_cast<int>(CountOf(safe_path)));
  log::Writef("FMOD bridge: %s result=%d mode=0x%x sound=%p path=%s", label,
              result, mode, sound != nullptr ? *sound : nullptr, safe_path);
}

}  // namespace

bool StartsWithIgnoreCaseAscii(const char* text, const char* literal,
                               size_t length) noexcept {
  if (text == nullptr || literal == nullptr) {
    return false;
  }

  for (size_t index = 0; index < length; ++index) {
    char character = text[index];
    if (character == '\0') {
      return false;
    }
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character | 0x20);
    }

    char expected = literal[index];
    if (expected >= 'A' && expected <= 'Z') {
      expected = static_cast<char>(expected | 0x20);
    }
    if (character != expected) {
      return false;
    }
  }
  return true;
}

const char* NormalizeAssetPath(const char* name_or_data, char* buffer,
                               size_t capacity) noexcept {
  if (name_or_data == nullptr || buffer == nullptr || capacity == 0) {
    return name_or_data;
  }

  const char* source = name_or_data;
  if (StartsWithIgnoreCaseAscii(source, "file://", 7) && source[7] == '/') {
    source += 8;
  } else if (StartsWithIgnoreCaseAscii(source, "ms-appx://", 10) &&
             source[10] == '/') {
    source += 11;
  }

  size_t written = 0;
  while (source[written] != '\0' && written + 1 < capacity) {
    char character = source[written];
    if (character == '/') {
      character = '\\';
    }
    buffer[written] = character;
    ++written;
  }
  buffer[written] = '\0';
  return buffer;
}

Result InitializeSystem(System* system, int max_channels, unsigned int flags,
                        void* extra_driver_data) noexcept {
  Api& api = ApiTable();
  const OutputChain chain = BuildOutputChain(RequestedOutput());
  Result result = kOk;

  for (int index = 0; index < chain.count; ++index) {
    const int output = chain.values[index];

    if (output != 0 && api.setOutput != nullptr) {
      result = api.setOutput(system, output);
      char label[256] = {};
      ::wsprintfA(label, "setOutput(%s)", OutputTypeName(output));
      log::Writef("FMOD bridge: %s -> %d (%s)", label, result,
                  ErrorText(result));
      if (result != kOk) {
        continue;
      }
    }

    SelectFirstDriver(system, output);

    result = api.init(system, max_channels, flags, extra_driver_data);
    log::Writef(
        "FMOD bridge: init output=%s channels=%d flags=0x%x result=%d (%s)",
        OutputTypeName(output), max_channels, flags, result, ErrorText(result));
    if (result == kOk) {
      break;
    }
  }

  if (result == kOk && api.getOutput != nullptr) {
    int output = -1;
    const Result query = api.getOutput(system, &output);
    log::Writef("FMOD bridge: getOutput result=%d output=%d", query, output);
  }
  return result;
}

Result CreateSoundWithNormalizedPath(System* system, const char* name_or_data,
                                     unsigned int mode, void* extra_info,
                                     Sound** sound) noexcept {
  char path[780] = {};
  const char* normalized = NormalizeAssetPath(name_or_data, path, sizeof(path));
  const Result result =
      ApiTable().createSound(system, normalized, mode, extra_info, sound);
  LogCreateResult("createSound", result, mode, sound, normalized);
  return result;
}

Result CreateStreamWithNormalizedPath(System* system, const char* name_or_data,
                                      unsigned int mode, void* extra_info,
                                      Sound** sound) noexcept {
  char path[780] = {};
  const char* normalized = NormalizeAssetPath(name_or_data, path, sizeof(path));
  const Result result =
      ApiTable().createStream(system, normalized, mode, extra_info, sound);
  LogCreateResult("createStream", result, mode, sound, normalized);
  return result;
}

void ForceAudible(ChannelGroup* group, const char* label) noexcept {
  if (group == nullptr || IsSilent(group) || !EnsureLoaded()) {
    return;
  }

  Api& api = ApiTable();
  Result mute_result = kOk;
  Result volume_result = kOk;
  if (api.setMute != nullptr) {
    mute_result = api.setMute(group, false);
  }
  if (api.setVolume != nullptr) {
    volume_result = api.setVolume(group, 1.0f);
  }
  log::Writef("FMOD bridge: force audible (%s) group=%p mute=%d volume=%d",
              label != nullptr ? label : "channel group", group, mute_result,
              volume_result);
}

void ReassertForegroundAudio() noexcept {
  const HWND window = app_window::Handle();
  Api& api = ApiTable();
  if (window == nullptr || ::GetForegroundWindow() != window ||
      api.setMute == nullptr) {
    return;
  }

  ChannelGroup* groups[] = {MasterGroup(), SoundGroup(), MusicGroup()};
  for (ChannelGroup* group : groups) {
    if (group != nullptr && !IsSilent(group)) {
      api.setMute(group, false);
    }
  }
}

Result ApplyMutePolicy(void* control, bool requested, bool* applied) noexcept {
  bool effective = requested;
  if (requested) {
    const HWND window = app_window::Handle();
    if (window != nullptr && ::GetForegroundWindow() == window) {
      effective = false;
    }
  }
  if (applied != nullptr) {
    *applied = effective;
  }
  return ApiTable().setMute(control, effective);
}

}  // namespace shim::fmod::detail
