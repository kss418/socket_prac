#include <iostream>
#include "../include/unique_fd.hpp"
#include "../include/addr.hpp"
#include "../include/fd_helper.hpp"
#include "../include/session.hpp"
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"

int main(){
    auto addr_exp = get_addr_client("127.0.0.1", "8080");
    if(!addr_exp){
        handle_error("get_addr_client failed", addr_exp);
        return 1;
    }

    auto server_fd_exp = make_server_fd(addr_exp->get());
    if(!server_fd_exp){
        handle_error("make_server_fd failed", server_fd_exp);
        return 1;
    }

    unique_fd server_fd = std::move(*server_fd_exp);
    socket_info si{};

    auto session_exp = echo_client(server_fd.get(), si);
    if(!session_exp){
        handle_error("echo_client failed", session_exp);
        return 1;
    }

    return 0;
}