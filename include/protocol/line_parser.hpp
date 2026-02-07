#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace line_parser {
    bool has_line(std::string_view recv_buf);
    std::optional<std::string> parse_line(std::string& recv_buf);
}
