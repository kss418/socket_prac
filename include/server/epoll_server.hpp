#pragma once
#include "core/error_code.hpp"
#include "net/io_helper.hpp"
#include "reactor/epoll_registry.hpp"
#include "reactor/event_loop.hpp"
#include "server/epoll_listener.hpp"
#include "server/epoll_acceptor.hpp"
#include "protocol/command_codec.hpp"
#include "core/thread_pool.hpp"
#include <stop_token>

class epoll_server{
    epoll_registry registry;
    epoll_listener listener;
    thread_pool pool{};

    void handle_send(int fd, socket_info& si);
    bool handle_recv(int fd, socket_info& si, uint32_t event);
    void handle_close(int fd, socket_info& si);
    void handle_client_error(int fd, uint32_t event);
    bool handle_execute(int fd);
public:
    epoll_server(const epoll_server&) = delete;
    epoll_server& operator=(const epoll_server&) = delete;

    epoll_server(epoll_server&& other) noexcept = delete;
    epoll_server& operator=(epoll_server&& other) noexcept = delete;

    static std::expected <epoll_server, error_code> create(const char* port);
    epoll_server(epoll_registry registry, epoll_listener listener);
    std::expected <void, error_code> run();
    std::expected <void, error_code> run(const std::stop_token& stop_token);
};
