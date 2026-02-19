#include "protocol/line_parser.hpp"
#include "net/io_helper.hpp"

bool line_parser::has_line(std::string_view recv_buf){
    return recv_buf.find('\n') != std::string_view::npos;
}

bool line_parser::has_line(const offset_buffer& recv_buf){
    return recv_buf.raw().find('\n', recv_buf.get_offset()) != std::string::npos;
}

std::optional<std::string> line_parser::parse_line(offset_buffer& buf){
    const std::size_t start = buf.get_offset();
    std::size_t pos = buf.raw().find('\n', start);
    if(pos == std::string::npos) return std::nullopt;

    std::string line = buf.raw().substr(start, pos - start);
    buf.set_offset(pos + 1);
    if(!buf.clear_if_done()) buf.compact_if_needed();
    return line;
}
