#include "../include/server_factory.hpp"
#include "../include/addr.hpp"
#include "../include/epoll_listener.hpp"
#include "../include/epoll_wakeup.hpp"

std::expected <epoll_server, error_code> server_factory::create_server(const char* port){
    auto addr_exp = get_addr_server(port);
    if(!addr_exp){
        handle_error("create_server/get_addr_server failed", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto listen_fd_exp = epoll_listener::make_listener(addr_exp->get());
    if(!listen_fd_exp){
        handle_error("create_server/make_listener failed", listen_fd_exp);
        return std::unexpected(listen_fd_exp.error());
    }

    auto wakeup_exp = epoll_wakeup::create();
    if(!wakeup_exp){
        handle_error("create_server/epoll_wakeup::create failed", wakeup_exp);
        return std::unexpected(wakeup_exp.error());
    }

    epoll_registry registry(std::move(*wakeup_exp));
    epoll_listener listener = std::move(*listen_fd_exp);
    return epoll_server(std::move(registry), std::move(listener));
}
