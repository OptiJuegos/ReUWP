#pragma once

#include "shim/common.h"

namespace shim::runtime_config {

struct Settings {
  bool local_save_path = false;
  unsigned int min_render_distance = 1;
};

bool Initialize(const wchar_t* install_directory) noexcept;
const Settings& Get() noexcept;
const wchar_t* FilePath() noexcept;

}  // namespace shim::runtime_config
