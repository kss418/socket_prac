#include "protocol/line_parser.hpp"

bool line_parser::has_line(std::string_view recv_buf){
    return recv_buf.find('\n') != std::string_view::npos;
}

std::optional<std::string> line_parser::parse_line(std::string& buf){
    std::size_t pos = buf.find('\n');
    if(pos == std::string::npos) return std::nullopt;

    std::string line = buf.substr(0, pos);
    buf.erase(0, pos + 1);
    return line;
}
