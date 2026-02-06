#pragma once
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"
#include "../include/epoll_registry.hpp"
#include "../include/event_loop.hpp"
#include "../include/epoll_listener.hpp"
#include "../include/epoll_acceptor.hpp"

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
};
