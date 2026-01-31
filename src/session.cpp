#include "../include/session.hpp"
#include "../include/fd_helper.hpp"
#include <iostream>
#include <string_view>
#include <utility>

std::expected <void, error_code> echo_server(int client_fd, socket_info& si){
    std::array <char, BUF_SIZE> buf{};
    auto ep_exp = make_peer_endpoint(client_fd);
    if(!ep_exp) return std::unexpected(ep_exp.error());
    endpoint ep = std::move(*ep_exp);
    
    auto ep_str_exp = ep.get_string();
    if(!ep_str_exp) return std::unexpected(ep_str_exp.error());
    std::string ep_str = std::move(*ep_str_exp);

    std::cout << ep_str << " is connected\n";
    while(true){
        ssize_t recv_byte = ::recv(client_fd, buf.data(), buf.size(), 0);
        if(recv_byte == 0){
            std::cout << ep_str << " is disconnected\n";
            return {};
        }

        if(recv_byte == -1){
            int ec = errno;
            if(ec == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        si.append(buf.data(), static_cast<std::size_t>(recv_byte));
        auto flush_send_exp = flush_send(client_fd, si);
        if(!flush_send_exp){
            std::cerr << ep_str << " send failed" << to_string(flush_send_exp.error()) << "\n";
            continue;
        }

        size_t send_byte = *flush_send_exp;
        std::cout << ep_str << " sends " << send_byte << " byte" << (send_byte == 1 ? "\n" : "s\n");
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

            if(now == 0) return {};
            recv_byte += now;
            std::cout << std::string_view(buf.data(), now) << "\n";
        }
    }
}