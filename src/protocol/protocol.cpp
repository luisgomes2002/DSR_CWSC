#include "protocol/protocol.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace dsr
{
bool printable(std::string_view s)
{
    return std::all_of(s.begin(), s.end(), [](unsigned char c)
                       { return c >= 32 && c <= 126; });
}
bool name_ok(std::string_view s)
{
    return !s.empty() && s.size() <= 24 && std::all_of(s.begin(), s.end(), [](unsigned char c)
                                                       { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                                (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}
std::vector<std::string> split(const std::string &s)
{
    std::vector<std::string> result;
    size_t start = 0;
    for (;;)
    {
        auto end = s.find('\t', start);
        result.push_back(s.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            return result;
        start = end + 1;
    }
}
unsigned number(std::string_view value, unsigned low, unsigned high)
{
    if (value.empty() || value.size() > 10)
        throw std::runtime_error("Numero invalido");
    std::uint64_t n = 0;
    for (char c : value)
    {
        if (c < '0' || c > '9')
            throw std::runtime_error("Numero invalido");
        n = n * 10 + static_cast<unsigned>(c - '0');
    }
    if (n < low || n > high)
        throw std::runtime_error("Numero fora do limite");
    return static_cast<unsigned>(n);
}

bool queue(std::string &out, const std::string &line)
{
    if (line.size() > MaxFrame || out.size() + line.size() + 1 > MaxQueue)
        return false;
    out += line;
    out += '\n';
    return true;
}
} // namespace dsr


