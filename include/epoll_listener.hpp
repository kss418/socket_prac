#pragma once
#include "../include/error_code.hpp"
#include "../include/epoll_wakeup.hpp"
#include "../include/unique_fd.hpp"

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
