#pragma once

#include <string>

namespace dsr
{
struct Options;

// Runs the terminal client; returns a process exit status.
int join(const Options &o, const std::string &secret);
} // namespace dsr


