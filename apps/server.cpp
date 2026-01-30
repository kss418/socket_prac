#include <iostream>
#include <array>
#include <cstring>
#include "../include/unique_fd.hpp"
#include "../include/addr.hpp"
#include "../include/fd_helper.hpp"

constexpr int BUF_SIZE = 4096;

std::expected <void, int> echo_session(int client_fd){
    std::array <char, BUF_SIZE> buf{};
    while(true){
        ssize_t recv_byte = ::recv(client_fd, buf.data(), buf.size(), 0);
        if(recv_byte == 0) return {};
        if(recv_byte == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(ec);
        }

        ssize_t send_byte = 0;
        while(send_byte < recv_byte){
            ssize_t now = ::send(client_fd, buf.data() + send_byte, recv_byte - send_byte, MSG_NOSIGNAL);
            if(now == -1){
                int ec = errno;
                if(ec == EINTR) continue;
                return std::unexpected(ec);
            }

            if(now == 0) return std::unexpected(EPIPE);
            send_byte += now;
        }
    }
}

int main(){
    auto addr_exp = get_addr_server("8080");
    if(!addr_exp){
        std::cerr << "try_get_addr failed: " << ::gai_strerror(addr_exp.error()) << "\n";
        return 1;
    }

    auto listen_fd_exp = make_listen_fd(addr_exp->get());
    if(!listen_fd_exp){
        std::cerr << "make_listen_fd failed: " << std::strerror(listen_fd_exp.error()) << "\n";
        return 1;
    }

    unique_fd listen_fd = std::move(*listen_fd_exp);
    while(true){
        auto client_fd_exp = make_client_fd(listen_fd.get());
        if(!client_fd_exp){
            std::cerr << "make_client_fd failed " << std::strerror(client_fd_exp.error()) << "\n";
            continue;
        }

        unique_fd client_fd = std::move(*client_fd_exp);
        auto session_exp = echo_session(client_fd.get());
        if(!session_exp){
            std::cerr << "echo_session failed: " << std::strerror(session_exp.error()) << "\n";
            continue;
        }
    }

    return 0;
}