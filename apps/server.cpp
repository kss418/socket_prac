#include <iostream>
#include <array>
#include <cstring>
#include "../include/unique_fd.hpp"
#include "../include/addr.hpp"
#include "../include/fd_helper.hpp"
#include "../include/session.hpp"

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