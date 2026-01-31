#include "../include/session.hpp"
#include "../include/fd_helper.hpp"
#include <iostream>
#include <string_view>
#include <utility>

std::expected <void, error_code> echo_server(int client_fd, socket_info& si){
    std::string buf;
    auto ep_exp = make_peer_endpoint(client_fd);
    if(!ep_exp) return std::unexpected(ep_exp.error());
    endpoint ep = std::move(*ep_exp);
    
    auto ep_str_exp = ep.get_string();
    if(!ep_str_exp) return std::unexpected(ep_str_exp.error());
    std::string ep_str = std::move(*ep_str_exp);

    std::cout << ep_str << " is connected\n";
    while(true){
        auto recv_ret_exp = drain_recv(client_fd, buf);
        if(!recv_ret_exp) return std::unexpected(recv_ret_exp.error());
        auto recv_ret = std::move(*recv_ret_exp);

        if(recv_ret.closed){
            std::cout << ep_str << " is disconnected\n";
            return {};
        }

        si.append(buf.data(), static_cast<std::size_t>(recv_ret.byte));
        si.append("\n");
        auto flush_send_exp = flush_send(client_fd, si);
        if(!flush_send_exp) return std::unexpected(flush_send_exp.error());

        size_t send_byte = *flush_send_exp;
        std::cout << ep_str << " sends " << send_byte << " byte" << (send_byte == 1 ? "\n" : "s\n");
    }
}

std::expected <void, error_code> echo_client(int server_fd, socket_info& si){
    std::string buf;
    while(true){
        std::string s;
        if(!std::getline(std::cin, s)) return {};

        s.push_back('\n');
        si.append(s);
        auto flush_send_exp = flush_send(server_fd, si);
        if(!flush_send_exp){
            std::cerr << "send failed" << to_string(flush_send_exp.error()) << "\n";
            continue;
        }

        auto recv_ret_exp = drain_recv(server_fd, buf);
        if(!recv_ret_exp) return std::unexpected(recv_ret_exp.error());
        flush_recv(buf);
        if(recv_ret_exp->closed) return {};
    }
}