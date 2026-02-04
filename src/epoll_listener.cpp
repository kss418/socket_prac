#include "../include/epoll_listener.hpp"
#include <sys/epoll.h>

std::expected <epoll_listener, error_code> epoll_listener::make_listener(addrinfo* head){
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
                int epfd = ::epoll_create1(EPOLL_CLOEXEC);
                if(epfd == -1){
                    int ec = errno;
                    handle_error("make_listener/epoll_create1 failed", error_code::from_errno(ec));
                    return std::unexpected(error_code::from_errno(ec));
                }

                return epoll_listener{
                    unique_fd{epfd}, unique_fd(std::move(fd))
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
int epoll_listener::get_epfd() const{ return epfd.get(); }