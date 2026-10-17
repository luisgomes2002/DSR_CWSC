#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace dsr
{
// Polls stdin without a background thread or blocking the network loop.
class ConsoleInput
{
public:
    ConsoleInput();
    bool eof() const;
    std::vector<std::string> poll();

private:
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    bool console = false, ended = false;
    std::string pending;
};
} // namespace dsr


