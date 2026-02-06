#include "../include/epoll_listener.hpp"
#include "../include/epoll_utility.hpp"
#include <cerrno>
#include <sys/epoll.h>

epoll_listener::epoll_listener(epoll_wakeup wakeup, unique_fd listen_fd) :
    epoll_wakeup(std::move(wakeup)), listen_fd(std::move(listen_fd)){}

std::expected <epoll_listener, error_code> epoll_listener::create(addrinfo* head){
    int ec = 0;
    for(addrinfo* p = head; p; p = p->ai_next){
        unique_fd fd(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if(!fd){ ec = errno; continue; }

        int use = 1;
        if(::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &use, sizeof(use)) == -1){
            ec = errno;
            continue;
        }

        if(::bind(fd.get(), p->ai_addr, p->ai_addrlen) == 0){
            if(::listen(fd.get(), SOMAXCONN) == 0){
                auto wakeup_exp = epoll_wakeup::create();
                if(!wakeup_exp){
                    handle_error("create/epoll_wakeup::create failed", wakeup_exp);
                    return std::unexpected(wakeup_exp.error());
                }

                auto add_exp = epoll_utility::add_fd(wakeup_exp->get_epfd(), fd.get(), EPOLLIN);
                if(!add_exp){
                    handle_error("create/add_listener_fd failed", add_exp);
                    return std::unexpected(add_exp.error());
                }

                return epoll_listener{
                    std::move(*wakeup_exp), unique_fd(std::move(fd))
                };
            }
            ec = errno;
            return std::unexpected(error_code::from_errno(ec));
        }

        ec = errno;
    }

    if(ec == 0) ec = EINVAL;
    return std::unexpected(error_code::from_errno(ec));
}

int epoll_listener::get_fd() const{ return listen_fd.get(); }
