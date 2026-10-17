#include "ui/console_input.hpp"
#include "protocol/protocol.hpp"

#include <conio.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace dsr
{
ConsoleInput::ConsoleInput()
{
    DWORD mode = 0;
    console = GetConsoleMode(handle, &mode) != 0;
}

bool ConsoleInput::eof() const { return ended; }

std::vector<std::string> ConsoleInput::poll()
{
    std::vector<std::string> lines;
    auto consume = [&](char c)
    {
        if (c == '\r')
        {
            if (!console)
                return;
            c = '\n';
        }
        if (c == '\n')
        {
            lines.push_back(pending);
            pending.clear();
            if (console)
                std::cout << '\n';
        }
        else if (c == '\b' && console)
        {
            if (!pending.empty())
            {
                pending.pop_back();
                std::cout << "\b \b";
            }
        }
        else
        {
            if (pending.size() >= MaxFrame)
                throw std::runtime_error("Entrada local excede limite");
            pending += c;
            if (console)
                std::cout << c << std::flush;
        }
    };
    if (ended)
        return lines;
    if (console)
    {
        for (int i = 0; i < 256 && _kbhit(); ++i)
        {
            const int c = _getch();
            if (c == 0 || c == 224)
            {
                (void)_getch();
                continue;
            }
            if (c == 26)
            {
                ended = true;
                break;
            }
            consume(static_cast<char>(c));
        }
    }
    else
    {
        DWORD available = 0;
        const DWORD type = GetFileType(handle);
        if (type == FILE_TYPE_PIPE && !PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr))
        {
            if (GetLastError() == ERROR_BROKEN_PIPE)
                ended = true;
            else
                throw std::runtime_error("Nao foi possivel consultar stdin");
        }
        else if (type == FILE_TYPE_DISK || (type == FILE_TYPE_PIPE && available))
        {
            std::array<char, 512> buf{};
            DWORD count = 0;
            const DWORD wanted = type == FILE_TYPE_DISK ? static_cast<DWORD>(buf.size()) : (std::min)(available, static_cast<DWORD>(buf.size()));
            if (!ReadFile(handle, buf.data(), wanted, &count, nullptr) || !count)
                ended = true;
            else
                for (DWORD i = 0; i < count; ++i)
                    consume(buf[i]);
        }
        else if (type != FILE_TYPE_PIPE)
            ended = true;
    }
    if (ended && !pending.empty())
    {
        lines.push_back(pending);
        pending.clear();
    }
    return lines;
}
} // namespace dsr


