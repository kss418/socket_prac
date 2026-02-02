#include "../include/chat_client.hpp"
#include "../include/addr.hpp"

std::expected <void, error_code> chat_client::connect(const char* ip, const char* port){
    auto addr_exp = get_addr_client(ip, port);
    if(!addr_exp){
        handle_error("get_addr_client failed", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto server_fd_exp = make_server_fd(addr_exp->get());
    if(!server_fd_exp){
        handle_error("make_server_fd failed", server_fd_exp);
        return std::unexpected(server_fd_exp.error());
    }

    server_fd = std::move(*server_fd_exp);
    si = {};
    return {};
}

std::expected <void, error_code> chat_client::run(){
    while(true){
        std::string s;
        if(!std::getline(std::cin, s)) return {};

        s.push_back('\n');
        si.append(s);
        auto flush_send_exp = flush_send(server_fd.get(), si);
        if(!flush_send_exp){
            handle_error("flush_send failed", flush_send_exp);
            continue;
        }

        auto recv_ret_exp = drain_recv(server_fd.get(), si);
        if(!recv_ret_exp){
            handle_error("drain_recv failed", recv_ret_exp);
            return std::unexpected(recv_ret_exp.error());
        }

        flush_recv(si.recv_buf);
        if(recv_ret_exp->closed) return {};
    }

    return {};
}