#include "../include/io_helper.hpp"

bool socket_info::buf_clear(){
    if(buf.size() != offset) return false;
    buf.clear();
    offset = 0;
    return true;
}

bool socket_info::buf_compact(){
    if(offset < 8192) return false;
    if (offset * 2 < buf.size()) return false; 
    buf.erase(0, offset);
    offset = 0;
    return true;
}

void socket_info::append(std::string_view sv){
    buf += sv;
}

void socket_info::append(const char* p, std::size_t n){
    buf.append(p, n);
}

std::expected <size_t, error_code> flush_send(int fd, socket_info& si){
    std::size_t send_byte = 0;
    while(si.offset < si.buf.size()){
        ssize_t now = ::send(fd, si.buf.data() + si.offset, si.buf.size() - si.offset, MSG_NOSIGNAL);
        if(now == -1){
            int ec = errno;
            if(ec == EINTR) continue;
            if (ec == EAGAIN || ec == EWOULDBLOCK){
                si.buf_compact();
                return send_byte;
            }
            return std::unexpected(error_code::from_errno(ec));
        }

        if(now == 0) return std::unexpected(error_code::from_errno(EPIPE));
        si.offset += static_cast<std::size_t>(now);
        send_byte += static_cast<std::size_t>(now);
    }

    si.buf_clear();
    return send_byte;
}