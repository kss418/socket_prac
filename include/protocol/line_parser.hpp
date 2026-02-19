#pragma once
#include <optional>
#include <string>
#include <string_view>

class offset_buffer;

namespace line_parser {
    bool has_line(std::string_view recv_buf);
    bool has_line(const offset_buffer& recv_buf);

    std::optional<std::string> parse_line(offset_buffer& recv_buf);
}
