#pragma once
#include "socket_prac/core/error_code.hpp"
#include "socket_prac/core/unique_fd.hpp"

class epoll_wakeup{
protected:
    unique_fd epfd;
    unique_fd wake_fd;
public:
    epoll_wakeup() = default;
    epoll_wakeup(unique_fd epfd, unique_fd wake_fd);

    epoll_wakeup(const epoll_wakeup&) = delete;
    epoll_wakeup& operator=(const epoll_wakeup&) = delete;

    epoll_wakeup(epoll_wakeup&&) noexcept = default;
    epoll_wakeup& operator=(epoll_wakeup&&) noexcept = default;

    static std::expected<epoll_wakeup, error_code> create();
    void request_wakeup() const;
    void consume_wakeup() const;

    int get_epfd() const;
    int get_wake_fd() const;
};
