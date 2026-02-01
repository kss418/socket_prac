#include "../include/session.hpp"
#include "../include/fd_helper.hpp"
#include <iostream>
#include <string_view>
#include <utility>

std::expected <void, error_code> echo_client(int server_fd, socket_info& si){
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

        auto recv_ret_exp = drain_recv(server_fd, si);
        if(!recv_ret_exp) return std::unexpected(recv_ret_exp.error());
        flush_recv(si.recv_buf);
        if(recv_ret_exp->closed) return {};
    }
}