#pragma once

#include <string_view>

namespace ftx {

inline constexpr std::string_view kVersion = "0.1.0";

[[nodiscard]] std::string_view version() noexcept;

}  // namespace ftx
