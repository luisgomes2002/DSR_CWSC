#pragma once

#include <string>

namespace dsr
{
struct Options;

// Runs the terminal client; returns a process exit status.
int join(const Options &options, const std::string &room_password);
} // namespace dsr


