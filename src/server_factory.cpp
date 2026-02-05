#include "../include/server_factory.hpp"
#include "../include/addr.hpp"
#include "../include/epoll_listener.hpp"
#include "../include/unique_fd.hpp"
#include "../include/epoll_utility.hpp"
#include <sys/eventfd.h>

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
        handle_error("create_server/epoll_create1 failed", error_code::from_errno(ec));
        return std::unexpected(error_code::from_errno(ec));
    }

    unique_fd evfd = unique_fd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if(!evfd){
        int ec = errno;
        handle_error("create_server/eventfd failed", error_code::from_errno(ec));
        return std::unexpected(error_code::from_errno(ec));
    }

    auto add_exp = epoll_utility::add_fd(epfd.get(), evfd.get(), EPOLLIN);
    if(!add_exp){
        handle_error("create_server/add_fd failed", add_exp);
        return std::unexpected(add_exp.error());
    }

    epoll_registry registry = {std::move(epfd), std::move(evfd)};
    epoll_listener listener = std::move(*listen_fd_exp);
    return epoll_server(std::move(registry), std::move(listener));
}
