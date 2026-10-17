#pragma once

#include <string>

namespace dsr
{
struct ClientCommand
{
    std::string frame;
    bool quit = false;
};

ClientCommand client_command(const std::string &line);
} // namespace dsr
