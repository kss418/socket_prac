#include "../include/session.hpp"
#include <iostream>
#include <string_view>

std::expected <void, error_code> echo_server(int client_fd){
    std::array <char, BUF_SIZE> buf{};
    while(true){
        ssize_t recv_byte = ::recv(client_fd, buf.data(), buf.size(), 0);
        if(recv_byte == 0) return {};
        if(recv_byte == -1){
            int ec = errno;
            if(ec == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        ssize_t send_byte = 0;
        while(send_byte < recv_byte){
            ssize_t now = ::send(client_fd, buf.data() + send_byte, recv_byte - send_byte, MSG_NOSIGNAL);
            if(now == -1){
                int ec = errno;
                if(ec == EINTR) continue;
                return std::unexpected(error_code::from_errno(ec));
            }

            if(now == 0) return std::unexpected(error_code::from_errno(EPIPE));
            send_byte += now;
        }
    }
}

std::expected <void, error_code> echo_client(int server_fd){
    std::array <char, BUF_SIZE> buf{};
    while(true){
        std::string s;
        if(!std::getline(std::cin, s)) return {};

        ssize_t send_byte = 0;
        while(send_byte < s.size()){
            ssize_t now = ::send(server_fd, s.data() + send_byte, s.size() - send_byte, MSG_NOSIGNAL);
            if(now == -1){
                int ec = errno;
                if(ec == EINTR) continue;
                return std::unexpected(error_code::from_errno(ec));
            }

            if(now == 0) return std::unexpected(error_code::from_errno(EPIPE));
            send_byte += now;
        }
        
        ssize_t recv_byte = 0;
        while(recv_byte < send_byte){
            ssize_t now = ::recv(server_fd, buf.data(), std::min<size_t>(send_byte - recv_byte, buf.size()), 0);
            if(now == -1){
                int ec = errno;
                if(ec == EINTR) continue;
                return std::unexpected(error_code::from_errno(ec));
            }

            recv_byte += now;
            std::cout << std::string_view(buf.data(), now);
        }
    }
}