#include "ui/client_commands.hpp"
#include "protocol/entity_messages.hpp"
#include "protocol/protocol.hpp"

#include <sstream>
#include <stdexcept>

namespace dsr
{
ClientCommand client_command(const std::string &line)
{
    if (!printable(line)) throw std::runtime_error("Only printable ASCII is supported by this prototype");
    if (line == "/quit") return {"QUIT", true};
    if (line == "/who") return {"WHO"};
    if (line == "/players") return {"PLAYERS"};
    if (line == "/move" || line.rfind("/move ", 0) == 0)
    {
        std::istringstream input(line);
        std::vector<std::string> tokens;
        for (std::string token; input >> token;) tokens.push_back(token);
        if (tokens.size() != 4) throw std::runtime_error("Use /move X Y Z; use a period for decimals");
        return {"MOVE\t" + position_fields(parse_position(tokens, 1))};
    }
    if (line.rfind("/send ", 0) == 0)
    {
        const auto end = line.find(' ', 6);
        if (end == std::string::npos || end + 1 == line.size() || line.size() - end - 1 > MaxText)
            throw std::runtime_error("Use /send ID text (up to 1024 bytes)");
        const auto recipient_id = number(std::string_view(line).substr(6, end - 6), 1, 0xffffffffu);
        return {"SEND\t" + std::to_string(recipient_id) + "\t" + line.substr(end + 1)};
    }
    if (!line.empty() && line.front() == '/') throw std::runtime_error("Unknown command");
    if (line.empty() || line.size() > MaxText) throw std::runtime_error("Text must contain 1-1024 bytes");
    return {"SAY\t" + line};
}
} // namespace dsr
