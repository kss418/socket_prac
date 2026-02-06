#pragma once
#include "socket_prac/core/error_code.hpp"
#include "socket_prac/reactor/epoll_wakeup.hpp"
#include "socket_prac/core/unique_fd.hpp"

class epoll_listener : public epoll_wakeup{
    unique_fd listen_fd;
public:
    epoll_listener(const epoll_listener&) = delete;
    epoll_listener& operator=(const epoll_listener&) = delete;

    epoll_listener(epoll_listener&&) noexcept = default;
    epoll_listener& operator=(epoll_listener&&) noexcept = default;
    
    explicit epoll_listener(epoll_wakeup wakeup, unique_fd listen_fd);
    static std::expected <epoll_listener, error_code> create(addrinfo* head);

    int get_fd() const;
};
