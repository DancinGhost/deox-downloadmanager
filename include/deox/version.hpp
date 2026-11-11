#pragma once

#include <string_view>

namespace deox {

inline constexpr int VERSION_MAJOR = 1;
inline constexpr int VERSION_MINOR = 0;
inline constexpr int VERSION_PATCH = 0;

inline constexpr std::string_view VERSION_STRING = "1.0.0";
inline constexpr std::string_view PROJECT_NAME = "deox";
inline constexpr std::string_view PROJECT_DESCRIPTION = 
    "High-performance memory-safe download manager in C++23 (Functional Core, Imperative Shell)";

} // namespace deox
