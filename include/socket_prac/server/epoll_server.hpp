#pragma once
#include "socket_prac/core/error_code.hpp"
#include "socket_prac/net/io_helper.hpp"
#include "socket_prac/reactor/epoll_registry.hpp"
#include "socket_prac/reactor/event_loop.hpp"
#include "socket_prac/server/epoll_listener.hpp"
#include "socket_prac/server/epoll_acceptor.hpp"
#include <stop_token>

class epoll_server{
    epoll_registry registry;
    epoll_listener listener;

    void handle_send(int fd, socket_info& si);
    void handle_recv(int fd, socket_info& si, uint32_t event);
    void handle_close(int fd, socket_info& si);
public:
    epoll_server(const epoll_server&) = delete;
    epoll_server& operator=(const epoll_server&) = delete;

    epoll_server(epoll_server&& other) noexcept = default;
    epoll_server& operator=(epoll_server&& other) noexcept = default;

    static std::expected <epoll_server, error_code> create(const char* port);
    epoll_server(epoll_registry registry, epoll_listener listener);
    std::expected <void, error_code> run();
    std::expected <void, error_code> run(const std::stop_token& stop_token);
};
