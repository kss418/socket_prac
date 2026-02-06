#pragma once
#include "core/error_code.hpp"
#include "core/unique_fd.hpp"
#include "server/epoll_listener.hpp"
#include "reactor/epoll_registry.hpp"
#include "core/constant.hpp"
#include <array>
#include <stop_token>
#include <sys/epoll.h>

class epoll_acceptor{
    epoll_listener& listener;
    epoll_registry& registry;
    std::array<epoll_event, EVENT_SIZE> events;
    void handle_accept();
    std::expected <unique_fd, error_code> make_client_fd();
public:
    epoll_acceptor(const epoll_acceptor&) = delete;
    epoll_acceptor& operator=(const epoll_acceptor&) = delete;

    epoll_acceptor(epoll_acceptor&&) noexcept = default;
    epoll_acceptor& operator=(epoll_acceptor&&) noexcept = default;

    epoll_acceptor(epoll_listener& listener, epoll_registry& registry);
    std::expected <void, error_code> run(const std::stop_token& stop_token);
};
