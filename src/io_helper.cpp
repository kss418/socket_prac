#include "../include/io_helper.hpp"
#include "../include/ep_helper.hpp"
#include "../include/fd_helper.hpp"
#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <array>
#include <iostream>

bool socket_info::buf_clear(){
    if(send_buf.size() != offset) return false;
    send_buf.clear();
    offset = 0;
    return true;
}

bool socket_info::buf_compact(){
    if(offset < 8192) return false;
    if (offset * 2 < send_buf.size()) return false; 
    send_buf.erase(0, offset);
    offset = 0;
    return true;
}

void socket_info::append(std::string_view sv){
    send_buf += sv;
}

void socket_info::append(const char* p, std::size_t n){
    send_buf.append(p, n);
}

std::expected <std::size_t, error_code> flush_send(int fd, socket_info& si){
    std::size_t send_byte = 0;
    while(si.offset < si.send_buf.size()){
        ssize_t now = ::send(fd, si.send_buf.data() + si.offset, si.send_buf.size() - si.offset, MSG_NOSIGNAL);
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

std::expected <recv_info, error_code> drain_recv(int fd, socket_info& si){
    recv_info ret;
    std::array <char, BUF_SIZE> tmp{};
    while(true){
        ssize_t now = ::recv(fd, tmp.data(), tmp.size(), 0);
        if(now > 0){
            si.recv_buf.append(tmp.data(), static_cast<std::size_t>(now));
            ret.byte += static_cast<std::size_t>(now);
            if(si.recv_buf.find('\n') != std::string::npos) return ret;
            continue;
        }

        if(now == 0){
            ret.closed = true;
            return ret;
        }

        int ec = errno;
        if(ec == EINTR) continue;
        if(ec == EAGAIN || ec == EWOULDBLOCK) return ret;
        return std::unexpected(error_code::from_errno(ec));
    }
}

void flush_recv(std::string& recv_buf){
    while(true){
        auto pos = recv_buf.find('\n');
        if(pos == std::string::npos) return;
        std::cout << std::string_view(recv_buf.data(), pos) << "\n";
        recv_buf.erase(0, pos + 1);
    }
}

std::expected <void, error_code> register_listen_fd(int epfd, int fd){
    if(fd == -1) return std::unexpected(error_code::from_errno(EINVAL));

    auto nonblocking_exp = set_nonblocking(fd);
    if(!nonblocking_exp) return std::unexpected(nonblocking_exp.error());

    auto add_ep_exp = add_ep(epfd, fd, EPOLLIN);
    if(!add_ep_exp) return std::unexpected(add_ep_exp.error());
    return {};
}

