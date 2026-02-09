#pragma once
#include "core/error_code.hpp"
#include <expected>
#include <vector>
#include <string>
#include <string_view>
#include <variant>

namespace command_codec{
    struct cmd_say{ std::string text; };
    struct cmd_nick{ std::string nick; };
    struct cmd_response{ std::string text; };

    enum class decode_error : int{
        empty_line = 1,
        invalid_command,
        unexpected_argument
    };

    struct decode_info{
        std::string_view cmd;
        std::vector<std::string_view> args;
    };
    using command = std::variant<cmd_say, cmd_nick, cmd_response>;

    decode_info decode_line(std::string_view line);
    std::string_view erase_delimeter(std::string_view line);

    std::string encode(const command& cmd);
    std::expected <command, error_code> decode(std::string_view line);
    std::string decode_strerror(int code);
}
