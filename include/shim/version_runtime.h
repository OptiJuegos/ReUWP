#pragma once

#include "shim/common.h"

namespace shim::versions::v0132 {
WPARAM Run() noexcept;
}  // namespace shim::versions::v0132

namespace shim::versions::v01510 {
bool BringUp() noexcept;
void RunFrame() noexcept;
}  // namespace shim::versions::v01510

namespace shim::versions::v115 {
bool BringUp() noexcept;
void RunFrame() noexcept;
}  // namespace shim::versions::v115
