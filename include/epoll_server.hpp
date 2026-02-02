#pragma once
#include <array>
#include <sys/epoll.h>
#include <unordered_map>
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"

constexpr int EP_SIZE = 128;

class epoll_server{
    std::unordered_map <int, socket_info> socket_infos;
    std::array <epoll_event, EP_SIZE> events;
    const char* port;
    unique_fd epfd;
    unique_fd listen_fd;
public:
    epoll_server(const epoll_server&) = delete;
    epoll_server& operator=(const epoll_server&) = delete;

    epoll_server(epoll_server&& other) noexcept = default;
    epoll_server& operator=(epoll_server&& other) noexcept = default;

    epoll_server(const char* port = "8080") : port(port){}
    std::expected <void, error_code> init();
    std::expected <void, error_code> run();
};