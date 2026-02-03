#pragma once
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"
#include "../include/epoll_registry.hpp"
#include "../include/event_loop.hpp"

class epoll_server{
    epoll_registry ep_registry;
    unique_fd listen_fd;
    const char* port;

    void handle_accept();
    void handle_send(int fd, socket_info& si);
    void handle_recv(int fd, socket_info& si, uint32_t event);
    void handle_close(int fd, socket_info& si);
public:
    epoll_server(const epoll_server&) = delete;
    epoll_server& operator=(const epoll_server&) = delete;

    epoll_server(epoll_server&& other) noexcept = default;
    epoll_server& operator=(epoll_server&& other) noexcept = default;

    epoll_server(const char* port = "8080");
    std::expected <void, error_code> init();
    std::expected <void, error_code> run();
};