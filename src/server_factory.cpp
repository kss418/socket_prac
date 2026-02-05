#include "../include/server_factory.hpp"
#include "../include/addr.hpp"
#include "../include/epoll_listener.hpp"
#include "../include/unique_fd.hpp"

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

    unique_fd epfd = unique_fd{::epoll_create1(EPOLL_CLOEXEC)};
    if(!epfd){
        int ec = errno;
        handle_error("init/epoll_create1 failed", error_code::from_errno(ec));
        return std::unexpected(error_code::from_errno(ec));
    }

    epoll_registry ep_registry = {std::move(epfd)};
    epoll_listener listener = std::move(*listen_fd_exp);
    epoll_acceptor acceptor(listener, ep_registry);
    return epoll_server(std::move(ep_registry), std::move(listener), std::move(acceptor));
}
