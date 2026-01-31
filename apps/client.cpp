#include <iostream>
#include <cstring>
#include "../include/unique_fd.hpp"
#include "../include/addr.hpp"
#include "../include/fd_helper.hpp"
#include "../include/session.hpp"
#include "../include/error_code.hpp"

int main(){
    auto addr_exp = get_addr_client("127.0.0.1", "8080");
    if(!addr_exp){
        std::cerr << "try_get_addr failed: " << to_string(addr_exp.error()) << "\n";
        return 1;
    }

    auto server_fd_exp = make_server_fd(addr_exp->get());
    if(!server_fd_exp){
        std::cerr << "make_server_fd failed: " << to_string(server_fd_exp.error()) << "\n";
        return 1;
    }

    unique_fd server_fd = std::move(*server_fd_exp);
    while(true){
        auto session_exp = echo_client(server_fd.get());
        if(!session_exp){
            std::cerr << "echo_client failed: " << to_string(session_exp.error()) << "\n";
            continue;
        }
    }

    return 0;
}