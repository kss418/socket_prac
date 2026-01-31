#include <iostream>
#include <cstring>
#include <unordered_map>
#include "../include/unique_fd.hpp"
#include "../include/addr.hpp"
#include "../include/fd_helper.hpp"
#include "../include/session.hpp"
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"

int main(){
    auto addr_exp = get_addr_server("8080");
    if(!addr_exp){
        std::cerr << "get_addr_server failed: " << to_string(addr_exp.error()) << "\n";
        return 1;
    }

    auto listen_fd_exp = make_listen_fd(addr_exp->get());
    if(!listen_fd_exp){
        std::cerr << "make_listen_fd failed: " << to_string(listen_fd_exp.error()) << "\n";
        return 1;
    }

    std::unordered_map <int, socket_info> socket_infos;

    unique_fd listen_fd = std::move(*listen_fd_exp);
    while(true){
        auto client_fd_exp = make_client_fd(listen_fd.get());
        if(!client_fd_exp){
            std::cerr << "make_client_fd failed " << to_string(client_fd_exp.error()) << "\n";
            continue;
        }

        unique_fd client_fd = std::move(*client_fd_exp);
        auto session_exp = echo_server(client_fd.get(), socket_infos[client_fd.get()]);
        if(!session_exp){
            std::cerr << "echo_server failed: " << to_string(session_exp.error()) << "\n";
            socket_infos.erase(client_fd.get());
            continue;
        }
        socket_infos.erase(client_fd.get());
    }

    return 0;
}