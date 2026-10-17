#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace dsr
{
inline constexpr const char *ProtocolVersion = "3";
inline constexpr const char *BuildId = "dsr-network-lab-0.4";
inline constexpr std::size_t MaxFrame = 2048;
inline constexpr std::size_t MaxQueue = 65536;
inline constexpr std::size_t MaxText = 1024;

// Shared wire-format rules. This module has no Windows/socket dependencies.
bool printable(std::string_view s);
bool name_ok(std::string_view s);
std::vector<std::string> split(const std::string &s);
unsigned number(std::string_view value, unsigned low, unsigned high);
bool queue(std::string &out, const std::string &line);
} // namespace dsr


