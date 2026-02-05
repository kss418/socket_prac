#pragma once
#include "../include/error_code.hpp"
#include "../include/unique_fd.hpp"

class epoll_listener{
    unique_fd epfd;
    unique_fd listen_fd;
public:
    epoll_listener(const epoll_listener&) = delete;
    epoll_listener& operator=(const epoll_listener&) = delete;

    epoll_listener(epoll_listener&&) noexcept = default;
    epoll_listener& operator=(epoll_listener&&) noexcept = default;
    
    explicit epoll_listener(unique_fd epfd, unique_fd listen_fd);
    static std::expected <epoll_listener, error_code> make_listener(addrinfo* head);

    int get_fd() const;
    int get_epfd() const;
};
